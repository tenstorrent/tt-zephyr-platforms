/*
 * Copyright (c) 2026 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_grendel_clock_control

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(clock_control_tt_grendel, CONFIG_CLOCK_CONTROL_LOG_LEVEL);

#define CGM_ENABLES_OFFSET         0x00
#define CGM_FCW_INT_OFFSET         0x04
#define CGM_FCW_FRAC_OFFSET        0x08
#define CGM_PREDIV_OFFSET          0x0C
#define CGM_POSTDIV_ARRAY_OFFSET   0x10
#define CGM_POSTDIV_CONFIG_OFFSET  0x18
#define CGM_LOCK_CONFIG_OFFSET     0x1C
#define CGM_REG_UPDATE_OFFSET      0x20
#define CGM_OPEN_LOOP_OFFSET       0x38
#define CGM_LOCK_MONITOR_OFFSET    0x3C
#define CGM_SAMPLE_STROBE_OFFSET   0x40
#define CGM_STATUS_OFFSET          0x4C
#define CGM_FCW_INC_OFFSET         0x8C
#define CGM_SSC_HALF_PERIOD_OFFSET 0x90

#define CGM_ENABLE_BIT      BIT(0)
#define FREQ_ACQ_ENABLE_BIT BIT(1)
#define DROPOUT_ENABLE_BIT  BIT(2)

#define POSTDIV0_MASK GENMASK(2, 0)
#define POSTDIV1_MASK GENMASK(5, 3)
#define POSTDIV2_MASK GENMASK(8, 6)
#define POSTDIV3_MASK GENMASK(11, 9)

#define POSTDIV_CONFIG_BYPASS_THEN_SWITCH 0x0
#define POSTDIV_CONFIG_ALWAYS_POSTDIV     0x1
#define POSTDIV_CONFIG_FORCE_BYPASS       0x2
#define POSTDIV_CONFIG_FORCE_GATE         0x3

#define LOCK_DETECT_BIT BIT(0)

#define FCW_INT_MIN   16
#define FCW_INT_MAX   255
#define FCW_FRAC_BITS 14

#define POSTDIV_MIN 1
#define POSTDIV_MAX 4

#define CGM_NUM_OUTPUTS 4

struct clock_control_tt_grendel_config {
	uintptr_t base;
	uint32_t ref_clk;
};

struct clock_control_tt_grendel_data {
	struct k_spinlock lock;
};

static inline void cgm_strobe_reg_update(const struct clock_control_tt_grendel_config *cfg)
{
	sys_write32(BIT(0), cfg->base + CGM_REG_UPDATE_OFFSET);
}

static int cgm_wait_for_lock(const struct clock_control_tt_grendel_config *cfg)
{
	k_timepoint_t deadline =
		sys_timepoint_calc(K_MSEC(CONFIG_CLOCK_CONTROL_TT_GRENDEL_LOCK_TIMEOUT));

	do {
		if (sys_read32(cfg->base + CGM_STATUS_OFFSET) & LOCK_DETECT_BIT) {
			return 0;
		}
	} while (!K_TIMEOUT_EQ(sys_timepoint_timeout(deadline), K_NO_WAIT));

	return -ETIMEDOUT;
}

static uint32_t postdiv_mask_for_output(uint8_t output)
{
	switch (output) {
	case 0:
		return POSTDIV0_MASK;
	case 1:
		return POSTDIV1_MASK;
	case 2:
		return POSTDIV2_MASK;
	case 3:
		return POSTDIV3_MASK;
	default:
		return 0;
	}
}

static uint8_t cgm_get_postdiv(const struct clock_control_tt_grendel_config *cfg, uint8_t output)
{
	uint32_t reg = sys_read32(cfg->base + CGM_POSTDIV_ARRAY_OFFSET);

	return FIELD_GET(postdiv_mask_for_output(output), reg);
}

static void cgm_set_postdiv(const struct clock_control_tt_grendel_config *cfg, uint8_t output,
			    uint8_t postdiv)
{
	uint32_t mask = postdiv_mask_for_output(output);
	uint32_t reg = sys_read32(cfg->base + CGM_POSTDIV_ARRAY_OFFSET);

	reg &= ~mask;
	reg |= FIELD_PREP(mask, postdiv);
	sys_write32(reg, cfg->base + CGM_POSTDIV_ARRAY_OFFSET);
}

static void cgm_set_postdiv_config(const struct clock_control_tt_grendel_config *cfg,
				   uint8_t output, uint8_t config_val)
{
	uint32_t shift = output * 2;
	uint32_t reg = sys_read32(cfg->base + CGM_POSTDIV_CONFIG_OFFSET);

	reg &= ~(0x3 << shift);
	reg |= ((uint32_t)config_val << shift);
	sys_write32(reg, cfg->base + CGM_POSTDIV_CONFIG_OFFSET);
}

static uint8_t cgm_get_postdiv_config(const struct clock_control_tt_grendel_config *cfg,
				      uint8_t output)
{
	uint32_t shift = output * 2;
	uint32_t reg = sys_read32(cfg->base + CGM_POSTDIV_CONFIG_OFFSET);

	return (reg >> shift) & 0x3;
}

static int clock_control_tt_grendel_on(const struct device *dev, clock_control_subsys_t sys)
{
	const struct clock_control_tt_grendel_config *cfg = dev->config;
	struct clock_control_tt_grendel_data *data = dev->data;
	uint8_t output = (uint8_t)(uintptr_t)sys;
	k_spinlock_key_t key;
	uint32_t cgm_enable;
	int ret;

	if (output >= CGM_NUM_OUTPUTS) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);

	cgm_enable = sys_read32(cfg->base + CGM_ENABLES_OFFSET);

	if (!(cgm_enable & CGM_ENABLE_BIT)) {
		cgm_enable = CGM_ENABLE_BIT | FREQ_ACQ_ENABLE_BIT | DROPOUT_ENABLE_BIT;
		sys_write32(cgm_enable, cfg->base + CGM_ENABLES_OFFSET);
	}

	cgm_set_postdiv_config(cfg, output, POSTDIV_CONFIG_BYPASS_THEN_SWITCH);

	cgm_strobe_reg_update(cfg);

	ret = cgm_wait_for_lock(cfg);

	k_spin_unlock(&data->lock, key);
	return ret;
}

static int clock_control_tt_grendel_off(const struct device *dev, clock_control_subsys_t sys)
{
	const struct clock_control_tt_grendel_config *cfg = dev->config;
	struct clock_control_tt_grendel_data *data = dev->data;
	uint8_t output = (uint8_t)(uintptr_t)sys;
	k_spinlock_key_t key;

	if (output >= CGM_NUM_OUTPUTS) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);

	cgm_set_postdiv_config(cfg, output, POSTDIV_CONFIG_FORCE_GATE);
	cgm_strobe_reg_update(cfg);

	k_spin_unlock(&data->lock, key);
	return 0;
}

static int clock_control_tt_grendel_set_rate(const struct device *dev, clock_control_subsys_t sys,
					     clock_control_subsys_rate_t rate)
{
	const struct clock_control_tt_grendel_config *cfg = dev->config;
	struct clock_control_tt_grendel_data *data = dev->data;
	uint8_t output = (uint8_t)(uintptr_t)sys;
	uint32_t tgt_rate = (uint32_t)(uintptr_t)rate;
	k_spinlock_key_t key;
	uint8_t postdiv = 0;
	uint8_t fcw_int = 0;
	uint16_t fcw_frac = 0;
	uint32_t enables;
	bool found = false;
	int ret;

	if (output >= CGM_NUM_OUTPUTS) {
		return -EINVAL;
	}

	if (tgt_rate == 0 || cfg->ref_clk == 0) {
		return -EINVAL;
	}

	for (postdiv = POSTDIV_MIN; postdiv <= POSTDIV_MAX; postdiv++) {
		uint64_t fcw_fixed;

		fcw_fixed = ((uint64_t)tgt_rate << (postdiv + FCW_FRAC_BITS)) / cfg->ref_clk;
		fcw_int = (uint8_t)(fcw_fixed >> FCW_FRAC_BITS);
		fcw_frac = (uint16_t)(fcw_fixed & GENMASK(FCW_FRAC_BITS - 1, 0));

		if (fcw_int >= FCW_INT_MIN && fcw_int <= FCW_INT_MAX) {
			found = true;
			break;
		}
	}

	if (!found) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);

	enables = sys_read32(cfg->base + CGM_ENABLES_OFFSET);
	enables &= ~FREQ_ACQ_ENABLE_BIT;
	sys_write32(enables, cfg->base + CGM_ENABLES_OFFSET);

	sys_write32(fcw_int, cfg->base + CGM_FCW_INT_OFFSET);
	sys_write32(fcw_frac, cfg->base + CGM_FCW_FRAC_OFFSET);
	cgm_set_postdiv(cfg, output, postdiv);

	enables |= FREQ_ACQ_ENABLE_BIT;
	sys_write32(enables, cfg->base + CGM_ENABLES_OFFSET);

	cgm_strobe_reg_update(cfg);

	ret = cgm_wait_for_lock(cfg);

	k_spin_unlock(&data->lock, key);
	return ret;
}

static int clock_control_tt_grendel_get_rate(const struct device *dev, clock_control_subsys_t sys,
					     uint32_t *rate)
{
	const struct clock_control_tt_grendel_config *cfg = dev->config;
	struct clock_control_tt_grendel_data *data = dev->data;
	uint8_t output = (uint8_t)(uintptr_t)sys;
	k_spinlock_key_t key;
	uint8_t postdiv_config;
	uint8_t postdiv;
	uint32_t fcw_int;
	uint32_t fcw_frac;
	uint64_t fcw_combined;

	if (output >= CGM_NUM_OUTPUTS) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);

	postdiv_config = cgm_get_postdiv_config(cfg, output);

	if (postdiv_config == POSTDIV_CONFIG_FORCE_GATE) {
		k_spin_unlock(&data->lock, key);
		return -EAGAIN;
	}

	if (postdiv_config == POSTDIV_CONFIG_FORCE_BYPASS) {
		k_spin_unlock(&data->lock, key);
		*rate = cfg->ref_clk;
		return 0;
	}

	fcw_int = sys_read32(cfg->base + CGM_FCW_INT_OFFSET) & GENMASK(7, 0);
	fcw_frac = sys_read32(cfg->base + CGM_FCW_FRAC_OFFSET) & GENMASK(FCW_FRAC_BITS - 1, 0);
	postdiv = cgm_get_postdiv(cfg, output);

	k_spin_unlock(&data->lock, key);

	fcw_combined = ((uint64_t)fcw_int << FCW_FRAC_BITS) | fcw_frac;
	*rate = (uint32_t)(((uint64_t)cfg->ref_clk * fcw_combined) >> (FCW_FRAC_BITS + postdiv));

	return 0;
}

static int clock_control_tt_grendel_init(const struct device *dev)
{
	return 0;
}

static const struct clock_control_driver_api clock_control_tt_grendel_api = {
	.on = clock_control_tt_grendel_on,
	.off = clock_control_tt_grendel_off,
	.get_rate = clock_control_tt_grendel_get_rate,
	.set_rate = clock_control_tt_grendel_set_rate,
};

#define CLOCK_CONTROL_TT_GRENDEL_INIT(_inst)                                                       \
	static struct clock_control_tt_grendel_data clock_control_tt_grendel_data_##_inst;         \
                                                                                                   \
	static const struct clock_control_tt_grendel_config                                        \
		clock_control_tt_grendel_config_##_inst = {                                        \
			.base = DT_REG_ADDR(DT_DRV_INST(_inst)),                                   \
			.ref_clk = DT_INST_PROP(_inst, ref_clk),                                   \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(_inst, clock_control_tt_grendel_init, NULL,                          \
			      &clock_control_tt_grendel_data_##_inst,                              \
			      &clock_control_tt_grendel_config_##_inst, POST_KERNEL,               \
			      CONFIG_CLOCK_CONTROL_INIT_PRIORITY, &clock_control_tt_grendel_api);

DT_INST_FOREACH_STATUS_OKAY(CLOCK_CONTROL_TT_GRENDEL_INIT)
