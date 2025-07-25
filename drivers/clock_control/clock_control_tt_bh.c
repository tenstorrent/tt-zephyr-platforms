/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_bh_clock_control

#include <stdint.h>

#include <tenstorrent/post_code.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control/clock_control_tt_bh.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys_clock.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

LOG_MODULE_REGISTER(clock_control_tt_bh, CONFIG_CLOCK_CONTROL_LOG_LEVEL);

#define PLL_LOCK_TIMEOUT_MS 400

#define PLL_CNTL_0_OFFSET             0x00
#define PLL_CNTL_1_OFFSET             0x04
#define PLL_CNTL_2_OFFSET             0x08
#define PLL_CNTL_3_OFFSET             0x0C
#define PLL_CNTL_4_OFFSET             0x10
#define PLL_CNTL_5_OFFSET             0x14
#define PLL_CNTL_6_OFFSET             0x18
#define PLL_USE_POSTDIV_OFFSET        0x1C
#define PLL_REFCLK_SEL_OFFSET         0x20
#define PLL_USE_FINE_DIVIDER_1_OFFSET 0x24
#define PLL_USE_FINE_DIVIDER_2_OFFSET 0x28
#define FINE_DUTYC_ADJUST_OFFSET      0x2C
#define CLK_COUNTER_EN_OFFSET         0x30
#define CLK_COUNTER_0_OFFSET          0x34
#define CLK_COUNTER_1_OFFSET          0x38
#define CLK_COUNTER_2_OFFSET          0x3C
#define CLK_COUNTER_3_OFFSET          0x40
#define CLK_COUNTER_4_OFFSET          0x44
#define CLK_COUNTER_5_OFFSET          0x48
#define CLK_COUNTER_6_OFFSET          0x4C
#define CLK_COUNTER_7_OFFSET          0x50

#define VCO_MIN_FREQ                            1600
#define VCO_MAX_FREQ                            5000
#define CLK_COUNTER_REFCLK_PERIOD               1000
#define PLL_CNTL_WRAPPER_PLL_LOCK_REG_ADDR      0x80020040
#define PLL_CNTL_WRAPPER_REFCLK_PERIOD_REG_ADDR 0x8002002C

typedef struct {
	uint32_t pll0_lock: 1;
	uint32_t pll1_lock: 1;
	uint32_t pll2_lock: 1;
	uint32_t pll3_lock: 1;
	uint32_t pll4_lock: 1;
} pll_cntl_wrapper_lock_fields;

typedef union {
	uint32_t val;
	pll_cntl_wrapper_lock_fields f;
} pll_cntl_wrapper_lock_reg;

typedef struct {
	uint32_t reset: 1;
	uint32_t pd: 1;
	uint32_t reset_lock: 1;
	uint32_t pd_bandgap: 1;
	uint32_t bypass: 1;
} pll_cntl_0_fields;

typedef union {
	uint32_t val;
	pll_cntl_0_fields f;
} pll_cntl_0_reg;

typedef struct {
	uint32_t refdiv: 8;
	uint32_t postdiv: 8;
	uint32_t fbdiv: 16;
} pll_cntl_1_fields;

typedef union {
	uint32_t val;
	pll_cntl_1_fields f;
} pll_cntl_1_reg;

typedef struct {
	uint32_t ctrl_bus1: 8;
	uint32_t ctrl_bus2: 8;
	uint32_t ctrl_bus3: 8;
	uint32_t ctrl_bus4: 8;
} pll_cntl_2_fields;

typedef union {
	uint32_t val;
	pll_cntl_2_fields f;
} pll_cntl_2_reg;

typedef struct {
	uint32_t ctrl_bus5: 8;
	uint32_t test_bus: 8;
	uint32_t lock_detect1: 16;
} pll_cntl_3_fields;

typedef union {
	uint32_t val;
	pll_cntl_3_fields f;
} pll_cntl_3_reg;

typedef struct {
	uint32_t postdiv0: 8;
	uint32_t postdiv1: 8;
	uint32_t postdiv2: 8;
	uint32_t postdiv3: 8;
} pll_cntl_5_fields;

typedef union {
	uint32_t val;
	pll_cntl_5_fields f;
} pll_cntl_5_reg;

typedef struct {
	uint32_t pll_use_postdiv0: 1;
	uint32_t pll_use_postdiv1: 1;
	uint32_t pll_use_postdiv2: 1;
	uint32_t pll_use_postdiv3: 1;
	uint32_t pll_use_postdiv4: 1;
	uint32_t pll_use_postdiv5: 1;
	uint32_t pll_use_postdiv6: 1;
	uint32_t pll_use_postdiv7: 1;
} pll_use_postdiv_fields;

typedef union {
	uint32_t val;
	pll_use_postdiv_fields f;
} pll_use_postdiv_reg;

typedef struct {
	pll_cntl_1_reg pll_cntl_1;
	pll_cntl_2_reg pll_cntl_2;
	pll_cntl_3_reg pll_cntl_3;
	pll_cntl_5_reg pll_cntl_5;
	pll_use_postdiv_reg use_postdiv;
} PLLSettings;

struct clock_control_tt_bh_config {
	uint8_t inst;

	uint32_t refclk_rate;

	uintptr_t base;
	size_t size;

	PLLSettings init_settings;
};

struct clock_control_tt_bh_data {
	struct k_spinlock lock;
};

static const uint8_t bh_clock_to_postdiv[] = {
	[CLOCK_CONTROL_TT_BH_CLOCK_AICLK] = 0,      [CLOCK_CONTROL_TT_BH_CLOCK_ARCCLK] = 0,
	[CLOCK_CONTROL_TT_BH_CLOCK_AXICLK] = 1,     [CLOCK_CONTROL_TT_BH_CLOCK_APBCLK] = 2,
	[CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_0] = 0, [CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_1] = 1,
	[CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_2] = 2, [CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_3] = 3,
	[CLOCK_CONTROL_TT_BH_CLOCK_GDDRMEMCLK] = 0,
};

static const bool enabled[] = {
	IS_ENABLED(DT_DRV_INST(0)), IS_ENABLED(DT_DRV_INST(1)), IS_ENABLED(DT_DRV_INST(2)),
	IS_ENABLED(DT_DRV_INST(3)), IS_ENABLED(DT_DRV_INST(4)),
};

const struct device *devs[] = {
	DEVICE_DT_GET_OR_NULL(DT_DRV_INST(0)), DEVICE_DT_GET_OR_NULL(DT_DRV_INST(1)),
	DEVICE_DT_GET_OR_NULL(DT_DRV_INST(2)), DEVICE_DT_GET_OR_NULL(DT_DRV_INST(3)),
	DEVICE_DT_GET_OR_NULL(DT_DRV_INST(4)),
};

extern void Wait(uint32_t cycles);

static inline uint32_t TimerGetCyclesForNsTime(uint32_t ns)
{
#define NS_PER_REFCLK 20

	return (ns + NS_PER_REFCLK - 1) / NS_PER_REFCLK;
}

/* FIXME: add k_busy_wait_ns() or k_busy_wait_cycles() for < 1 microsecond granularity */
static inline void WaitNs(uint32_t ns)
{
	uint32_t cycles = TimerGetCyclesForNsTime(ns);

	Wait(cycles);
}

static void clock_control_enable_clk_counters(const struct device *dev)
{
	const struct clock_control_tt_bh_config *config =
		(const struct clock_control_tt_bh_config *)dev->config;

	sys_write32(CLK_COUNTER_REFCLK_PERIOD, PLL_CNTL_WRAPPER_REFCLK_PERIOD_REG_ADDR);
	sys_write32(0xff, config->base + CLK_COUNTER_EN_OFFSET);
}

static void clock_control_tt_bh_config_vco(const struct device *dev, const PLLSettings *settings)
{
	const struct clock_control_tt_bh_config *config =
		(const struct clock_control_tt_bh_config *)dev->config;

	/* refdiv, postdiv, fbdiv */
	sys_write32(settings->pll_cntl_1.val, config->base + PLL_CNTL_1_OFFSET);
	/* FOUT4PHASEEN, FOUTPOSTDIVEN */
	sys_write32(settings->pll_cntl_2.val, config->base + PLL_CNTL_2_OFFSET);
	/* Disable SSCG */
	sys_write32(settings->pll_cntl_3.val, config->base + PLL_CNTL_3_OFFSET);
}

static void clock_control_tt_bh_config_ext_postdivs(const struct device *dev,
						    const PLLSettings *settings)
{
	const struct clock_control_tt_bh_config *config =
		(const struct clock_control_tt_bh_config *)dev->config;

	/* Disable postdivs before changing postdivs */
	sys_write32(0x0, config->base + PLL_USE_POSTDIV_OFFSET);
	/* Set postdivs */
	sys_write32(settings->pll_cntl_5.val, config->base + PLL_CNTL_5_OFFSET);
	/* Enable postdivs */
	sys_write32(settings->use_postdiv.val, config->base + PLL_USE_POSTDIV_OFFSET);
}

static bool clock_control_tt_bh_wait_lock(uint8_t inst)
{
	pll_cntl_wrapper_lock_reg pll_lock_reg;
	uint64_t start = k_uptime_get();

	do {
		pll_lock_reg.val = sys_read32(PLL_CNTL_WRAPPER_PLL_LOCK_REG_ADDR);
		if (pll_lock_reg.val & BIT(inst)) {
			return true;
		}
	} while (k_uptime_get() - start < CONFIG_CLOCK_CONTROL_TT_BH_LOCK_TIMEOUT_MS);

	return false;
}

static uint32_t clock_control_tt_bh_get_ext_postdiv(uint8_t postdiv_index,
						    pll_cntl_5_reg pll_cntl_5,
						    pll_use_postdiv_reg use_postdiv)
{
	uint32_t postdiv_value;
	bool postdiv_enabled;

	switch (postdiv_index) {
	case 0:
		postdiv_value = pll_cntl_5.f.postdiv0;
		postdiv_enabled = use_postdiv.f.pll_use_postdiv0;
		break;
	case 1:
		postdiv_value = pll_cntl_5.f.postdiv1;
		postdiv_enabled = use_postdiv.f.pll_use_postdiv1;
		break;
	case 2:
		postdiv_value = pll_cntl_5.f.postdiv2;
		postdiv_enabled = use_postdiv.f.pll_use_postdiv2;
		break;
	case 3:
		postdiv_value = pll_cntl_5.f.postdiv3;
		postdiv_enabled = use_postdiv.f.pll_use_postdiv3;
		break;
	default:
		CODE_UNREACHABLE;
	}
	if (postdiv_enabled) {
		uint32_t eff_postdiv;

		if (postdiv_value == 0) {
			eff_postdiv = 0;
		} else if (postdiv_value <= 16) {
			eff_postdiv = postdiv_value + 1;
		} else {
			eff_postdiv = (postdiv_value + 1) * 2;
		}

		return eff_postdiv;
	} else {
		return 1;
	}
}

static uint32_t clock_control_tt_bh_calculate_fbdiv(uint32_t refclk_rate, uint32_t target_freq_mhz,
						    pll_cntl_1_reg pll_cntl_1,
						    pll_cntl_5_reg pll_cntl_5,
						    pll_use_postdiv_reg use_postdiv,
						    uint8_t postdiv_index)
{
	uint32_t eff_postdiv =
		clock_control_tt_bh_get_ext_postdiv(postdiv_index, pll_cntl_5, use_postdiv);

	/* Means clock is disabled */
	if (eff_postdiv == 0) {
		return 0;
	}
	return target_freq_mhz * pll_cntl_1.f.refdiv * eff_postdiv / refclk_rate;
}

/* What we don't support: */
/* 1. PLL_CNTL_O.bypass */
/* 2. Internal bypass */
/* 3. Internal postdiv - PLL_CNTL_1.postdiv */
/* 4. Fractional feedback divider */
/* 5. Fine Divider */
static uint32_t clock_control_tt_bh_get_freq(const struct device *dev, uint8_t postdiv_index)
{
	pll_cntl_1_reg pll_cntl_1;
	pll_cntl_5_reg pll_cntl_5;
	pll_use_postdiv_reg use_postdiv;
	const struct clock_control_tt_bh_config *config = dev->config;

	pll_cntl_1.val = sys_read32(config->base + PLL_CNTL_1_OFFSET);
	pll_cntl_5.val = sys_read32(config->base + PLL_CNTL_5_OFFSET);
	use_postdiv.val = sys_read32(config->base + PLL_USE_POSTDIV_OFFSET);

	uint32_t eff_postdiv =
		clock_control_tt_bh_get_ext_postdiv(postdiv_index, pll_cntl_5, use_postdiv);

	/* Clock is disabled */
	if (eff_postdiv == 0) {
		return 0;
	}

	return (config->refclk_rate * pll_cntl_1.f.fbdiv) / (pll_cntl_1.f.refdiv * eff_postdiv);
}

static void clock_control_tt_bh_update(const struct device *dev, const PLLSettings *settings)
{
	pll_cntl_0_reg pll_cntl_0;
	const struct clock_control_tt_bh_config *config =
		(const struct clock_control_tt_bh_config *)dev->config;

	/* Before turning off PLL, bypass PLL so glitch free mux has no chance to switch */
	pll_cntl_0.val = sys_read32(config->base + PLL_CNTL_0_OFFSET);
	pll_cntl_0.f.bypass = 0;
	sys_write32(pll_cntl_0.val, config->base + PLL_CNTL_0_OFFSET);

	k_busy_wait(3);

	/* Power down PLL and disable PLL reset */
	pll_cntl_0.val = 0;
	sys_write32(pll_cntl_0.val, config->base + PLL_CNTL_0_OFFSET);

	clock_control_tt_bh_config_vco(dev, settings);

	/* Power sequence requires PLLEN get asserted 1us after all inputs are stable. */
	/* Wait 5x this time to be convervative */
	k_busy_wait(5);

	/* Power up PLLs */
	pll_cntl_0.f.pd = 1;
	sys_write32(pll_cntl_0.val, config->base + PLL_CNTL_0_OFFSET);

	/* Wait for PLLs to lock */
	clock_control_tt_bh_wait_lock(config->inst);

	/* Setup external postdivs */
	clock_control_tt_bh_config_ext_postdivs(dev, settings);

	k_busy_wait(300);

	/* Disable PLL bypass */
	pll_cntl_0.f.bypass = 1;
	sys_write32(pll_cntl_0.val, config->base + PLL_CNTL_0_OFFSET);

	k_busy_wait(300);
}

static int clock_control_tt_bh_on(const struct device *dev, clock_control_subsys_t sys)
{
	return -ENOSYS;
}

static int clock_control_tt_bh_off(const struct device *dev, clock_control_subsys_t sys)
{
	return -ENOSYS;
}

static int clock_control_tt_bh_async_on(const struct device *dev, clock_control_subsys_t sys,
					clock_control_cb_t cb, void *user_data)
{
	return -ENOSYS;
}

static int clock_control_tt_bh_get_rate(const struct device *dev, clock_control_subsys_t sys,
					uint32_t *rate)
{
	enum clock_control_tt_bh_clock bh_clock = (enum clock_control_tt_bh_clock)(uintptr_t)sys;

	if ((uintptr_t)sys >= ARRAY_SIZE(bh_clock_to_postdiv)) {
		LOG_ERR("Invalid clock %p", sys);
		return -ENOTSUP;
	}

	if (rate == NULL) {
		LOG_ERR("Invalid rate pointer");
		return -EINVAL;
	}

	*rate = clock_control_tt_bh_get_freq(dev, bh_clock_to_postdiv[bh_clock]);

	return 0;
}

static enum clock_control_status clock_control_tt_bh_get_status(const struct device *dev,
								clock_control_subsys_t sys)
{
	return CLOCK_CONTROL_STATUS_UNKNOWN;
}

static int clock_control_tt_bh_set_rate(const struct device *dev, clock_control_subsys_t sys,
					clock_control_subsys_rate_t rate)
{
	uint32_t fbdiv;
	k_spinlock_key_t key;
	uint32_t bh_rate = (uint32_t)(uintptr_t)rate;
	const struct clock_control_tt_bh_config *config =
		(const struct clock_control_tt_bh_config *)dev->config;
	struct clock_control_tt_bh_data *data = (struct clock_control_tt_bh_data *)dev->data;
	enum clock_control_tt_bh_clock bh_clock = (enum clock_control_tt_bh_clock)(uintptr_t)sys;
	PLLSettings pll_settings = config->init_settings;

	if ((uintptr_t)sys > CLOCK_CONTROL_TT_BH_CLOCK_GDDRMEMCLK) {
		LOG_ERR("Unsupported clock %d", bh_clock);
		return -ENOTSUP;
	}

	if (bh_clock == CLOCK_CONTROL_TT_BH_CLOCK_GDDRMEMCLK) {
		uint32_t vco_freq;

		fbdiv = clock_control_tt_bh_calculate_fbdiv(
			config->refclk_rate, bh_rate, pll_settings.pll_cntl_1,
			pll_settings.pll_cntl_5, pll_settings.use_postdiv, 0);
		if (fbdiv == 0) {
			LOG_ERR("Invalid fbdiv %u", fbdiv);
			return -EINVAL;
		}

		pll_settings.pll_cntl_1.f.fbdiv = fbdiv;
		vco_freq = (config->refclk_rate * pll_settings.pll_cntl_1.f.fbdiv) /
			   pll_settings.pll_cntl_1.f.refdiv;
		if (!IN_RANGE(vco_freq, VCO_MIN_FREQ, VCO_MAX_FREQ)) {
			LOG_ERR("Invalid vco_freq %u", vco_freq);
			return -ERANGE;
		}
	}

	if (k_spin_trylock(&data->lock, &key) < 0) {
		LOG_DBG("PLL %d busy", config->inst);
		return -EBUSY;
	}

	switch (bh_clock) {
	case CLOCK_CONTROL_TT_BH_CLOCK_GDDRMEMCLK:
		clock_control_tt_bh_update(dev, &pll_settings);
		break;

	case CLOCK_CONTROL_TT_BH_CLOCK_AICLK: {
		pll_cntl_1_reg pll_cntl_1;

		fbdiv = (bh_rate * 2) / config->refclk_rate;
		pll_cntl_1.val = sys_read32(config->base + PLL_CNTL_1_OFFSET);

		while (pll_cntl_1.f.fbdiv != fbdiv) {
			if (fbdiv > pll_cntl_1.f.fbdiv) {
				pll_cntl_1.f.fbdiv += 1;
			} else {
				pll_cntl_1.f.fbdiv -= 1;
			}

			sys_write32(pll_cntl_1.val, config->base + PLL_CNTL_1_OFFSET);
			k_busy_wait(100);
		}
	} break;
	default:
		CODE_UNREACHABLE;
	}

	k_spin_unlock(&data->lock, key);

	LOG_DBG("Set PLL %d to %u MHz", bh_clock, bh_rate);
	return 0;
}

static int clock_control_tt_bh_configure(const struct device *dev, clock_control_subsys_t sys,
					 void *option)
{
	k_spinlock_key_t key;
	const struct clock_control_tt_bh_config *config =
		(const struct clock_control_tt_bh_config *)dev->config;
	struct clock_control_tt_bh_data *data = (struct clock_control_tt_bh_data *)dev->data;
	enum clock_control_tt_bh_clock_config cc_opt =
		(enum clock_control_tt_bh_clock_config)(uintptr_t)option;

	if (cc_opt != CLOCK_CONTROL_TT_BH_CONFIG_BYPASS) {
		LOG_ERR("Invalid option %p", option);
		return -ENOTSUP;
	}

	if (k_spin_trylock(&data->lock, &key) < 0) {
		LOG_DBG("PLL %d busy", config->inst);
		return -EBUSY;
	}

	/* No need to bypass refclk as it's not support */

	pll_cntl_0_reg pll_cntl_0;

	/* Bypass PLL to refclk */
	pll_cntl_0.val = sys_read32(config->base + PLL_CNTL_0_OFFSET);
	pll_cntl_0.f.bypass = 0;

	sys_write32(pll_cntl_0.val, config->base + PLL_CNTL_0_OFFSET);

	k_busy_wait(3);

	/* Disable all external postdivs on all PLLs */
	sys_write32(0, config->base + PLL_USE_POSTDIV_OFFSET);

	k_spin_unlock(&data->lock, key);
	return 0;
}

static int clock_control_tt_bh_init_common(void)
{
	typedef struct {
		uint32_t reset: 1;
		uint32_t pd: 1;
		uint32_t reset_lock: 1;
		uint32_t pd_bgr: 1;
		uint32_t bypass: 1;
	} PLL_CNTL_PLL_CNTL_0_reg_t;

	typedef union {
		uint32_t val;
		PLL_CNTL_PLL_CNTL_0_reg_t f;
	} PLL_CNTL_PLL_CNTL_0_reg_u;

	PLL_CNTL_PLL_CNTL_0_reg_u pll_cntl_0;
	const struct device *dev;
	const struct clock_control_tt_bh_config *config;

	static bool clock_control_tt_bh_common_init_done;

	if (clock_control_tt_bh_common_init_done) {
		return 0;
	}

	ARRAY_FOR_EACH(enabled, i) {
		if (!enabled[i]) {
			continue;
		}

		dev = devs[i];
		config = dev->config;

		pll_cntl_0.val = sys_read32(config->base + PLL_CNTL_0_OFFSET);
		/* Before turning off PLL, bypass PLL so glitch free mux has no chance to switch */
		pll_cntl_0.f.bypass = 0;
		sys_write32(pll_cntl_0.val, config->base + PLL_CNTL_0_OFFSET);
	}

	k_busy_wait(3);

	ARRAY_FOR_EACH(enabled, i) {
		if (!enabled[i]) {
			continue;
		}

		dev = devs[i];
		config = dev->config;

		/* power down PLL, disable PLL reset */
		pll_cntl_0.val = 0;
		sys_write32(pll_cntl_0.val, config->base + PLL_CNTL_0_OFFSET);
	}

	ARRAY_FOR_EACH(enabled, i) {
		if (!enabled[i]) {
			continue;
		}

		dev = devs[i];
		config = dev->config;

		clock_control_tt_bh_config_vco(dev, &config->init_settings);
	}

	/* power sequence requires PLLEN get asserted 1us after all inputs are stable.  */
	/* wait 5x this time to be convervative */
	k_busy_wait(5);

	/* power up PLLs */
	pll_cntl_0.f.pd = 1;
	ARRAY_FOR_EACH(enabled, i) {
		if (!enabled[i]) {
			continue;
		}

		dev = devs[i];
		config = dev->config;

		sys_write32(pll_cntl_0.val, config->base + PLL_CNTL_0_OFFSET);
	}

	/* wait for PLLs to lock */
	ARRAY_FOR_EACH(enabled, i) {
		if (!enabled[i]) {
			continue;
		}

		clock_control_tt_bh_wait_lock(i);
	}

	/* setup external postdivs */
	ARRAY_FOR_EACH(enabled, i) {
		if (!enabled[i]) {
			continue;
		}

		dev = devs[i];
		config = dev->config;

		clock_control_tt_bh_config_ext_postdivs(dev, &config->init_settings);
	}

	/* FIXME: more accurate busy-wait API for Zephyr */
	WaitNs(300);

	/* disable PLL bypass */
	pll_cntl_0.f.bypass = 1;
	ARRAY_FOR_EACH(enabled, i) {
		if (!enabled[i]) {
			continue;
		}

		dev = devs[i];
		config = dev->config;

		sys_write32(pll_cntl_0.val, config->base + PLL_CNTL_0_OFFSET);
	}

	/* FIXME: more accurate busy-wait API for Zephyr */
	WaitNs(300);

	/* enable clock counters() */
	sys_write32(CLK_COUNTER_REFCLK_PERIOD, PLL_CNTL_WRAPPER_REFCLK_PERIOD_REG_ADDR);
	ARRAY_FOR_EACH(enabled, i) {
		if (!enabled[i]) {
			continue;
		}

		dev = devs[i];
		config = dev->config;

		sys_write32(0xff, config->base + CLK_COUNTER_EN_OFFSET);
	}

	clock_control_tt_bh_common_init_done = true;

	ARRAY_FOR_EACH(enabled, i) {
		if (!enabled[i]) {
			continue;
		}

		dev = devs[i];
		config = dev->config;

		LOG_DBG("Initialized PLL %zu: { %u, %u, %u, %u } MHz", i,
			clock_control_tt_bh_get_freq(dev, 0), clock_control_tt_bh_get_freq(dev, 1),
			clock_control_tt_bh_get_freq(dev, 2), clock_control_tt_bh_get_freq(dev, 3));
	}

	return 0;
}

static int clock_control_tt_bh_init(const struct device *dev)
{
	pll_cntl_0_reg pll_cntl_0;
	const struct clock_control_tt_bh_config *config =
		(const struct clock_control_tt_bh_config *)dev->config;

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP4);

	if (IS_ENABLED(CLOCK_CONTROL_TT_BH_PLL_COMMON_INIT)) {
		return clock_control_tt_bh_init_common();
	}

	/* Before turning off PLL, bypass PLL so glitch free mux has no chance to switch */
	pll_cntl_0.val = sys_read32(config->base + PLL_CNTL_0_OFFSET);
	pll_cntl_0.f.bypass = 0;

	sys_write32(pll_cntl_0.val, config->base + PLL_CNTL_0_OFFSET);

	k_busy_wait(3);

	/* Power down PLL and disable PLL reset */
	pll_cntl_0.val = 0;
	sys_write32(pll_cntl_0.val, config->base + PLL_CNTL_0_OFFSET);

	clock_control_tt_bh_config_vco(dev, &config->init_settings);

	/* Power sequence requires PLLEN get asserted 1us after all inputs are stable. */
	/* Wait 5x this time to be convervative */
	k_busy_wait(5);

	/* Power up PLLs */
	pll_cntl_0.f.pd = 1;
	sys_write32(pll_cntl_0.val, config->base + PLL_CNTL_0_OFFSET);

	/* Wait for PLLs to lock */
	if (!clock_control_tt_bh_wait_lock(config->inst)) {
		LOG_ERR("PLL %d failed to lock", config->inst);
		return -ETIMEDOUT;
	}

	/* Setup external postdivs */
	clock_control_tt_bh_config_ext_postdivs(dev, &config->init_settings);

	k_busy_wait(300);

	/* Disable PLL bypass */
	pll_cntl_0.f.bypass = 1;
	sys_write32(pll_cntl_0.val, config->base + PLL_CNTL_0_OFFSET);

	k_busy_wait(300);

	clock_control_enable_clk_counters(dev);

	ARRAY_FOR_EACH(enabled, i) {
		if (!enabled[i]) {
			continue;
		}

		dev = devs[i];
		config = dev->config;
	}

	LOG_DBG("Initialized PLL %u: { %u, %u, %u, %u } MHz", config->inst,
		clock_control_tt_bh_get_freq(dev, 0), clock_control_tt_bh_get_freq(dev, 1),
		clock_control_tt_bh_get_freq(dev, 2), clock_control_tt_bh_get_freq(dev, 3));

	return 0;
}

static const struct clock_control_driver_api clock_control_tt_bh_api = {
	.on = clock_control_tt_bh_on,
	.off = clock_control_tt_bh_off,
	.async_on = clock_control_tt_bh_async_on,
	.get_rate = clock_control_tt_bh_get_rate,
	.get_status = clock_control_tt_bh_get_status,
	.set_rate = clock_control_tt_bh_set_rate,
	.configure = clock_control_tt_bh_configure};

#define CLOCK_CONTROL_TT_BH_INIT(_inst)                                                            \
	static struct clock_control_tt_bh_data clock_control_tt_bh_data_##_inst;                   \
                                                                                                   \
	static const struct clock_control_tt_bh_config clock_control_tt_bh_config_##_inst = {      \
		.inst = _inst,                                                                     \
		.refclk_rate = DT_PROP(DT_INST_CLOCKS_CTLR(_inst), clock_frequency),               \
		.base = DT_REG_ADDR(DT_DRV_INST(_inst)),                                           \
		.size = DT_REG_SIZE(DT_DRV_INST(_inst)),                                           \
		.init_settings = {                                                                 \
			.pll_cntl_1 = {.f.refdiv = DT_INST_PROP(_inst, refdiv),                    \
				       .f.postdiv = DT_INST_PROP(_inst, postdiv),                  \
				       .f.fbdiv = DT_INST_PROP(_inst, fbdiv)},                     \
			.pll_cntl_2 = {.f.ctrl_bus1 = DT_INST_PROP(_inst, ctrl_bus1)},             \
			.pll_cntl_3 = {.f.ctrl_bus5 = DT_INST_PROP(_inst, ctrl_bus5)},             \
			.pll_cntl_5 = {.f.postdiv0 = DT_INST_PROP_BY_IDX(_inst, post_divs, 0),     \
				       .f.postdiv1 = DT_INST_PROP_BY_IDX(_inst, post_divs, 1),     \
				       .f.postdiv2 = DT_INST_PROP_BY_IDX(_inst, post_divs, 2),     \
				       .f.postdiv3 = DT_INST_PROP_BY_IDX(_inst, post_divs, 3)},    \
			.use_postdiv = {.f.pll_use_postdiv0 =                                      \
						DT_INST_PROP_BY_IDX(_inst, use_post_divs, 0),      \
					.f.pll_use_postdiv1 =                                      \
						DT_INST_PROP_BY_IDX(_inst, use_post_divs, 1),      \
					.f.pll_use_postdiv2 =                                      \
						DT_INST_PROP_BY_IDX(_inst, use_post_divs, 2),      \
					.f.pll_use_postdiv3 =                                      \
						DT_INST_PROP_BY_IDX(_inst, use_post_divs, 3)}}};   \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(                                                                     \
		_inst, clock_control_tt_bh_init, NULL, &clock_control_tt_bh_data_##_inst,          \
		&clock_control_tt_bh_config_##_inst, POST_KERNEL, 3, &clock_control_tt_bh_api);

DT_INST_FOREACH_STATUS_OKAY(CLOCK_CONTROL_TT_BH_INIT)
