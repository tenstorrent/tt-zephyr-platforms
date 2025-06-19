/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_bh_pll

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pll/pll_tt_bh.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys_clock.h>
#include <zephyr/sys/util.h>
#include <stdint.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pll_tt_bh);

#define PLL_COUNT 5

#define REFCLK_F_MHZ  50

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


#define VCO_MIN_FREQ              1600
#define VCO_MAX_FREQ              5000
#define CLK_COUNTER_REFCLK_PERIOD 1000
#define PLL_CNTL_WRAPPER_PLL_LOCK_REG_ADDR 0x80020040
#define PLL_CNTL_WRAPPER_REFCLK_PERIOD_REG_ADDR 0x8002002C

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
} PLL_CNTL_PLL_CNTL_1_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_PLL_CNTL_1_reg_t f;
} PLL_CNTL_PLL_CNTL_1_reg_u;

typedef struct {
	uint32_t ctrl_bus1: 8;
	uint32_t ctrl_bus2: 8;
	uint32_t ctrl_bus3: 8;
	uint32_t ctrl_bus4: 8;
} PLL_CNTL_PLL_CNTL_2_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_PLL_CNTL_2_reg_t f;
} PLL_CNTL_PLL_CNTL_2_reg_u;

typedef struct {
	uint32_t ctrl_bus5: 8;
	uint32_t test_bus: 8;
	uint32_t lock_detect1: 16;
} PLL_CNTL_PLL_CNTL_3_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_PLL_CNTL_3_reg_t f;
} PLL_CNTL_PLL_CNTL_3_reg_u;

typedef struct {
	uint32_t lock_detect2: 16;
	uint32_t lock_detect3: 16;
} PLL_CNTL_PLL_CNTL_4_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_PLL_CNTL_4_reg_t f;
} PLL_CNTL_PLL_CNTL_4_reg_u;

typedef struct {
	uint32_t postdiv0: 8;
	uint32_t postdiv1: 8;
	uint32_t postdiv2: 8;
	uint32_t postdiv3: 8;
} PLL_CNTL_PLL_CNTL_5_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_PLL_CNTL_5_reg_t f;
} PLL_CNTL_PLL_CNTL_5_reg_u;

typedef struct {
	uint32_t pll_use_postdiv0: 1;
	uint32_t pll_use_postdiv1: 1;
	uint32_t pll_use_postdiv2: 1;
	uint32_t pll_use_postdiv3: 1;
	uint32_t pll_use_postdiv4: 1;
	uint32_t pll_use_postdiv5: 1;
	uint32_t pll_use_postdiv6: 1;
	uint32_t pll_use_postdiv7: 1;
} PLL_CNTL_USE_POSTDIV_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_USE_POSTDIV_reg_t f;
} PLL_CNTL_USE_POSTDIV_reg_u;

typedef struct {
	uint32_t pll0_lock: 1;
	uint32_t pll1_lock: 1;
	uint32_t pll2_lock: 1;
	uint32_t pll3_lock: 1;
	uint32_t pll4_lock: 1;
} PLL_CNTL_WRAPPER_PLL_LOCK_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_WRAPPER_PLL_LOCK_reg_t f;
} PLL_CNTL_WRAPPER_PLL_LOCK_reg_u;

typedef struct {
	PLL_CNTL_PLL_CNTL_1_reg_u pll_cntl_1;
	PLL_CNTL_PLL_CNTL_2_reg_u pll_cntl_2;
	PLL_CNTL_PLL_CNTL_3_reg_u pll_cntl_3;
	PLL_CNTL_PLL_CNTL_5_reg_u pll_cntl_5;
	PLL_CNTL_USE_POSTDIV_reg_u use_postdiv;
} PLLSettings;

static const PLLSettings kPLLInitialSettings[PLL_COUNT] = {
	/* PLL0 - AICLK */
	{.pll_cntl_1 = {.f.refdiv = 2,
			.f.postdiv = 0,
			.f.fbdiv = 128},      /* 3200 MHz. Use VCO >= 2650 MHz:
						   * https://tenstorrent.atlassian.net/browse/SYS-777
						   */
	 .pll_cntl_2 = {.f.ctrl_bus1 = 0x18}, /* FOUT4PHASEEN, FOUTPOSTDIVEN bits asserted */
	 .pll_cntl_3 = {.f.ctrl_bus5 = 1},
	 .pll_cntl_5 = {.f.postdiv0 = 3,  /* = AICLK - 800 MHz */
			.f.postdiv1 = 0,  /* Disabled */
			.f.postdiv2 = 0,  /* Disabled */
			.f.postdiv3 = 0}, /* Disabled */
	 .use_postdiv = {.f.pll_use_postdiv0 = 1,
			 .f.pll_use_postdiv1 = 1,
			 .f.pll_use_postdiv2 = 1,
			 .f.pll_use_postdiv3 = 1}},
	/* PLL1 - ARCCLK, AXICLK, APBCLK */
	{.pll_cntl_1 = {.f.refdiv = 2, .f.postdiv = 0, .f.fbdiv = 192}, /* 4800 MHz */
	 .pll_cntl_2 = {.f.ctrl_bus1 = 0x18}, /* FOUT4PHASEEN, FOUTPOSTDIVEN bits asserted */
	 .pll_cntl_3 = {.f.ctrl_bus5 = 1},
	 .pll_cntl_5 = {.f.postdiv0 = 5,  /* ARCCLK - 800 MHz */
			.f.postdiv1 = 4,  /* AXICLK - 960 MHz to saturate PCIE DMA BW:
					   * https://tenstorrent.atlassian.net/browse/SYS-737
					   */
			.f.postdiv2 = 23, /* APBCLK - 100 MHz */
			.f.postdiv3 = 0}, /* Disabled */
	 .use_postdiv = {.f.pll_use_postdiv0 = 1,
			 .f.pll_use_postdiv1 = 1,
			 .f.pll_use_postdiv2 = 1,
			 .f.pll_use_postdiv3 = 1}},
	/* PLL2 - MACCLK, SECCLK */
	{.pll_cntl_1 = {.f.refdiv = 2, .f.postdiv = 0, .f.fbdiv = 68}, /* 1700 MHz */
	 .pll_cntl_2 = {.f.ctrl_bus1 = 0x18}, /* FOUT4PHASEEN, FOUTPOSTDIVEN bits asserted */
	 .pll_cntl_3 = {.f.ctrl_bus5 = 1},
	 .pll_cntl_5 = {.f.postdiv0 = 1,  /* MACCLK - 850 MHz */
			.f.postdiv1 = 0,  /* SECCLK - Disabled */
			.f.postdiv2 = 0,  /* Disabled */
			.f.postdiv3 = 0}, /* Disabled */
	 .use_postdiv = {.f.pll_use_postdiv0 = 1,
			 .f.pll_use_postdiv1 = 1,
			 .f.pll_use_postdiv2 = 1,
			 .f.pll_use_postdiv3 = 1}},
	/* PLL3 - GDDRMEMCLK */
	{.pll_cntl_1 = {.f.refdiv = 2, .f.postdiv = 0, .f.fbdiv = 120}, /* 3000 MHz */
	 .pll_cntl_2 = {.f.ctrl_bus1 = 0x18}, /* FOUT4PHASEEN, FOUTPOSTDIVEN bits asserted */
	 .pll_cntl_3 = {.f.ctrl_bus5 = 1},
	 .pll_cntl_5 = {.f.postdiv0 = 3,  /* GDDRMEMCLK - 750 MHz */
			.f.postdiv1 = 0,  /* Disabled */
			.f.postdiv2 = 0,  /* Disabled */
			.f.postdiv3 = 0}, /* Disabled */
	 .use_postdiv = {.f.pll_use_postdiv0 = 1,
			 .f.pll_use_postdiv1 = 1,
			 .f.pll_use_postdiv2 = 1,
			 .f.pll_use_postdiv3 = 1}},
	/* PLL4 - L2CPUCLK0,1,2,3 */
	{.pll_cntl_1 = {.f.refdiv = 2, .f.postdiv = 0, .f.fbdiv = 64}, /* 1600 MHz */
	 .pll_cntl_2 = {.f.ctrl_bus1 = 0x18}, /* FOUT4PHASEEN, FOUTPOSTDIVEN bits asserted */
	 .pll_cntl_3 = {.f.ctrl_bus5 = 1},
	 .pll_cntl_5 = {.f.postdiv0 = 1,  /* L2CPUCLK0 - 800 MHz */
			.f.postdiv1 = 1,  /* L2CPUCLK1 - 800 MHz */
			.f.postdiv2 = 1,  /* L2CPUCLK2 - 800 MHz */
			.f.postdiv3 = 1}, /* L2CPUCLK3 - 800 MHz */
	 .use_postdiv = {.f.pll_use_postdiv0 = 1,
			 .f.pll_use_postdiv1 = 1,
			 .f.pll_use_postdiv2 = 1,
			 .f.pll_use_postdiv3 = 1}},
};

struct pll_tt_bh_config {
	uint32_t ref_clk_hz;

	uintptr_t base;
	size_t size;
};

struct pll_tt_bh_data {
	struct k_spinlock lock;
	uint32_t current_rate;
};

static uint32_t pll_read_reg(const struct pll_tt_bh_config *config, uint8_t inst, uint32_t offset)
{
	return sys_read32(config->base + (config->size * inst) + offset);
}

static void pll_write_reg(const struct pll_tt_bh_config *config, uint8_t inst, uint32_t offset, uint32_t val)
{
	sys_write32(config->base + (config->size * inst) + offset, val);
}

static void enable_clk_counters(const struct pll_tt_bh_config *config)
{
	sys_write32(PLL_CNTL_WRAPPER_REFCLK_PERIOD_REG_ADDR, CLK_COUNTER_REFCLK_PERIOD);
	for (uint8_t i = 0; i < PLL_COUNT; ++i) {
		pll_write_reg(config, i, CLK_COUNTER_EN_OFFSET, 0xff);
	}
}

static void pll_config_vco(const struct pll_tt_bh_config *config, uint8_t inst)
{
	/* refdiv, postdiv, fbdiv */
	pll_write_reg(config, inst, PLL_CNTL_1_OFFSET, kPLLInitialSettings[inst].pll_cntl_1.val);
	/* FOUT4PHASEEN, FOUTPOSTDIVEN */
	pll_write_reg(config, inst, PLL_CNTL_2_OFFSET, kPLLInitialSettings[inst].pll_cntl_2.val);
	/* Disable SSCG */
	pll_write_reg(config, inst, PLL_CNTL_3_OFFSET, kPLLInitialSettings[inst].pll_cntl_3.val);
}

static void pll_config_ext_postdivs(const struct pll_tt_bh_config *config, uint8_t inst)
{
	/* Disable postdivs before changing postdivs */
	pll_write_reg(config, inst, PLL_USE_POSTDIV_OFFSET, 0x0);
	/* Set postdivs */
	pll_write_reg(config, inst, PLL_CNTL_5_OFFSET, kPLLInitialSettings[inst].pll_cntl_5.val);
	/* Enable postdivs */
	pll_write_reg(config, inst, PLL_USE_POSTDIV_OFFSET, kPLLInitialSettings[inst].use_postdiv.val);
}

/* Assume PLL Lock never times out */
static void pll_wait_lock(uint8_t inst)
{
	PLL_CNTL_WRAPPER_PLL_LOCK_reg_u pll_lock_reg;

	for (int i = 0; i < 400; ++i) {
		pll_lock_reg.val = sys_read32(PLL_CNTL_WRAPPER_PLL_LOCK_REG_ADDR);
		if (pll_lock_reg.val & BIT(inst)) {
			return;
		}
		k_busy_wait(1);
	}
}

static uint32_t GetExtPostdiv(uint8_t postdiv_index, PLL_CNTL_PLL_CNTL_5_reg_u pll_cntl_5,
		       PLL_CNTL_USE_POSTDIV_reg_u use_postdiv)
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
		__builtin_unreachable();
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

static uint32_t CalculateFbdiv(uint32_t target_freq_mhz, PLL_CNTL_PLL_CNTL_1_reg_u pll_cntl_1,
			PLL_CNTL_PLL_CNTL_5_reg_u pll_cntl_5,
			PLL_CNTL_USE_POSTDIV_reg_u use_postdiv, uint8_t postdiv_index)
{
	uint32_t eff_postdiv = GetExtPostdiv(postdiv_index, pll_cntl_5, use_postdiv);

	if (eff_postdiv == 0) {
		/* Means clock is disabled */
		return 0;
	}
	return target_freq_mhz * pll_cntl_1.f.refdiv * eff_postdiv / REFCLK_F_MHZ;
}

static uint32_t GetVcoFreq(PLL_CNTL_PLL_CNTL_1_reg_u pll_cntl_1)
{
	return (REFCLK_F_MHZ * pll_cntl_1.f.fbdiv) / pll_cntl_1.f.refdiv;
}

/* What we don't support: */
/* 1. PLL_CNTL_O.bypass */
/* 2. Internal bypass */
/* 3. Internal postdiv - PLL_CNTL_1.postdiv */
/* 4. Fractional feedback divider */
/* 5. Fine Divider */
static uint32_t CalculateFreqFromPllRegs(PLL_CNTL_PLL_CNTL_1_reg_u pll_cntl_1,
				  PLL_CNTL_PLL_CNTL_5_reg_u pll_cntl_5,
				  PLL_CNTL_USE_POSTDIV_reg_u use_postdiv, uint8_t postdiv_index)
{
	uint32_t refdiv = pll_cntl_1.f.refdiv;
	uint32_t fbdiv = pll_cntl_1.f.fbdiv;
	uint32_t eff_postdiv = GetExtPostdiv(postdiv_index, pll_cntl_5, use_postdiv);

	if (eff_postdiv == 0) {
		/* Means clock is disabled */
		return 0;
	}
	return (REFCLK_F_MHZ * fbdiv) / (refdiv * eff_postdiv);
}

static uint32_t GetFreqFromPll(const struct device *dev, uint8_t inst, uint8_t postdiv_index)
{
	PLL_CNTL_PLL_CNTL_1_reg_u pll_cntl_1;

	pll_cntl_1.val = pll_read_reg(dev->config, inst, PLL_CNTL_1_OFFSET);

	PLL_CNTL_PLL_CNTL_5_reg_u pll_cntl_5;

	pll_cntl_5.val = pll_read_reg(dev->config, inst, PLL_CNTL_5_OFFSET);
	
	PLL_CNTL_USE_POSTDIV_reg_u use_postdiv;

	use_postdiv.val = pll_read_reg(dev->config, inst, PLL_USE_POSTDIV_OFFSET);

	return CalculateFreqFromPllRegs(pll_cntl_1, pll_cntl_5, use_postdiv, postdiv_index);
}

uint32_t pll_GetAICLK(const struct device *dev)
{
	return GetFreqFromPll(dev->config, 0, 0);
}

uint32_t pll_GetARCCLK(const struct device *dev)
{
	return GetFreqFromPll(dev->config, 1, 0);
}

uint32_t pll_GetAXICLK(const struct device *dev)
{
	return GetFreqFromPll(dev->config, 1, 1);
}

uint32_t pll_GetAPBCLK(const struct device *dev)
{
	return GetFreqFromPll(dev->config, 1, 2);
}

uint32_t pll_GetL2CPUCLK(const struct device *dev, uint8_t l2cpu_num)
{
	return GetFreqFromPll(dev->config, 4, l2cpu_num);
}

void pll_bypass_all(const struct device *dev)
{
	const struct pll_tt_bh_config *config = (const struct pll_tt_bh_config *)dev->config;

	pll_cntl_0_reg pll_cntl_0;

	for (uint8_t i = 0; i < PLL_COUNT; ++i) {
		/* Bypass PLL to refclk */
		pll_cntl_0.val = pll_read_reg(config, i, PLL_CNTL_0_OFFSET);
		pll_cntl_0.f.bypass = 0;

		pll_write_reg(config, i, PLL_CNTL_0_OFFSET, pll_cntl_0.val);
	}

	k_busy_wait(3);

	for (uint8_t i = 0; i < PLL_COUNT; ++i) {
		/* Disable all external postdivs on all PLLs */
		pll_write_reg(config, i, PLL_USE_POSTDIV_OFFSET, 0);
	}
}

static void pll_update(const struct device *dev, const PLLSettings *pll_settings, uint8_t inst)
{
	const struct pll_tt_bh_config *config = (const struct pll_tt_bh_config *)dev->config;
	struct pll_tt_bh_data *data = (struct pll_tt_bh_data *)dev->data;

	k_spinlock_key_t key;

	if (k_spin_trylock(&data->lock, &key) < 0) {
		return;
	}

	pll_cntl_0_reg pll_cntl_0;

	/* Before turning off PLL, bypass PLL so glitch free mux has no chance to switch */
	pll_cntl_0.val = pll_read_reg(config, inst, PLL_CNTL_0_OFFSET);
	pll_cntl_0.f.bypass = 0;

	pll_write_reg(config, inst, PLL_CNTL_0_OFFSET, pll_cntl_0.val);

	k_busy_wait(3);

	/* Power down PLL and disable PLL reset */
	pll_cntl_0.val = 0;
	pll_write_reg(config, inst, PLL_CNTL_0_OFFSET, pll_cntl_0.val);

	pll_config_vco(config, inst);

	/* Power sequence requires PLLEN get asserted 1us after all inputs are stable.
	 * Wait 5x this time to be convervative */
	k_busy_wait(5);

	/* Power up PLLs */
	pll_cntl_0.f.pd = 1;
	pll_write_reg(config, inst, PLL_CNTL_0_OFFSET, pll_cntl_0.val);

	/* Wait for PLLs to lock */
	pll_wait_lock(inst);

	/* Setup external postdivs */
	pll_config_ext_postdivs(config, inst);

	k_busy_wait(300);

	/* Disable PLL bypass */
	pll_cntl_0.f.bypass = 1;
	pll_write_reg(config, inst, PLL_CNTL_0_OFFSET, pll_cntl_0.val);

	k_busy_wait(300);

	enable_clk_counters(config);

	k_spin_unlock(&data->lock, key);
}

static int pll_tt_bh_on(const struct device *dev, clock_control_subsys_t sys)
{
	pll_update(NULL, NULL, 0);
	pll_bypass_all(NULL);
	return -ENOSYS;
}

static int pll_tt_bh_off(const struct device *dev, clock_control_subsys_t sys)
{
	return -ENOSYS;
}

static int pll_tt_bh_async_on(const struct device *dev, clock_control_subsys_t sys,
				  clock_control_cb_t cb, void *user_data)
{
	return -ENOSYS;
}

static int pll_tt_bh_get_rate(const struct device *dev, clock_control_subsys_t sys, uint32_t *rate)
{
	return -ENOSYS;
}

static enum clock_control_status pll_tt_bh_get_status(const struct device *dev,
							  clock_control_subsys_t sys)
{
	return CLOCK_CONTROL_STATUS_UNKNOWN;
}

static int pll_tt_bh_set_rate(const struct device *dev, clock_control_subsys_t sys,
				  clock_control_subsys_rate_t rate)
{
	const struct pll_tt_bh_config *config = (const struct pll_tt_bh_config*)dev->config;
    uint8_t pll_idx = (uint8_t)(uintptr_t)sys;

    if (pll_idx >= PLL_COUNT) {
		return -EINVAL;
    }

    if (pll_idx == 4) { // GDDRMEMCLK
		uint32_t gddr_mem_clk_mhz = (uint32_t)rate / 1000000;
        PLLSettings pll_settings = {
			.pll_cntl_1 = {.f.refdiv = 2, .f.postdiv = 0}, /* 3000 MHz */
			.pll_cntl_2 = {.f.ctrl_bus1 = 0x18}, /* FOUT4PHASEEN, FOUTPOSTDIVEN bits asserted */
			.pll_cntl_3 = {.f.ctrl_bus5 = 1},
			.pll_cntl_5 = {.f.postdiv0 = 3,  /* GDDRMEMCLK */
					.f.postdiv1 = 0,  /* Disabled */
					.f.postdiv2 = 0,  /* Disabled */
					.f.postdiv3 = 0}, /* Disabled */
			.use_postdiv = {.f.pll_use_postdiv0 = 1,
					.f.pll_use_postdiv1 = 1,
					.f.pll_use_postdiv2 = 1,
					.f.pll_use_postdiv3 = 1}};
		uint32_t fbdiv = CalculateFbdiv(gddr_mem_clk_mhz, pll_settings.pll_cntl_1,
						pll_settings.pll_cntl_5, pll_settings.use_postdiv, 0);
		if (fbdiv == 0) {
			return -1;
		}
		pll_settings.pll_cntl_1.f.fbdiv = fbdiv;
		uint32_t vco_freq = GetVcoFreq(pll_settings.pll_cntl_1);

		if (!IN_RANGE(vco_freq, VCO_MIN_FREQ, VCO_MAX_FREQ)) {
			return -1;
		}

		pll_update(dev, &pll_settings, 4);
		return 0;
    } else if (pll_idx == 0) {
		uint32_t aiclk_in_mhz = (uint32_t)rate / 1000000;
		/* Assume: refdiv = 2, internal post div = 0, external post div = 1 */
		/* use fbdiv is enabled */

		/* calculate target FBDIV and actual aiclk */
		uint32_t target_fbdiv =
			(aiclk_in_mhz * 4) / REFCLK_F_MHZ; /* refdiv is 2, external postdiv is 1 */
		/* uint32_t target_aiclk = (REFCLK_F_MHZ * target_fbdiv) / 4; */
		uint32_t target_postdiv = 1;

		/* get current fbdiv and postdiv */
		PLL_CNTL_PLL_CNTL_1_reg_u pll_cntl_1;
		PLL_CNTL_PLL_CNTL_5_reg_u pll_cntl_5;

		pll_cntl_1.val = pll_read_reg(config, 0, PLL_CNTL_1_OFFSET);
		pll_cntl_5.val = pll_read_reg(config, 0, PLL_CNTL_5_OFFSET);

		/* baby step fbdiv and post div */
		while (pll_cntl_1.f.fbdiv != target_fbdiv) {
			if (target_fbdiv > pll_cntl_1.f.fbdiv) {
				pll_cntl_1.f.fbdiv += 1;
			} else if (target_fbdiv < pll_cntl_1.f.fbdiv) {
				pll_cntl_1.f.fbdiv -= 1;
			}
			pll_write_reg(config, 0, PLL_CNTL_1_OFFSET, pll_cntl_1.val);
			k_busy_wait(100); /* TODO: we need to characterize this timing */
		}

		while (pll_cntl_5.f.postdiv0 != target_postdiv) {
			if (target_postdiv > pll_cntl_5.f.postdiv0) {
				pll_cntl_5.f.postdiv0 += 1;
			} else if (target_postdiv < pll_cntl_5.f.postdiv0) {
				pll_cntl_5.f.postdiv0 -= 1;
			}
			pll_write_reg(config, 0, PLL_CNTL_5_OFFSET, pll_cntl_5.val);
			k_busy_wait(100); /* TODO: we need to characterize this timing */
		}
    } else {
        return -ENOTSUP;
    }

	return 0;
}

static int pll_tt_bh_configure(const struct device *dev, clock_control_subsys_t sys, void *data)
{
	return -ENOSYS;
}

static int pll_tt_bh_init(const struct device *dev)
{
	const struct pll_tt_bh_config *config = (const struct pll_tt_bh_config *)dev->config;
	struct pll_tt_bh_data *data = (struct pll_tt_bh_data *)dev->data;

	k_spinlock_key_t key;

	if (k_spin_trylock(&data->lock, &key) < 0) {
		return -EBUSY;
	}

	pll_cntl_0_reg pll_cntl_0;

	/* Before turning off PLL, bypass PLL so glitch free mux has no chance to switch */
	for (uint8_t i = 0; i < PLL_COUNT; ++i) {
		pll_cntl_0.val = pll_read_reg(config, i, PLL_CNTL_0_OFFSET);
		pll_cntl_0.f.bypass = 0;

		pll_write_reg(config, i, PLL_CNTL_0_OFFSET, pll_cntl_0.val);
	}

	k_busy_wait(3);

	/* Power down PLL and disable PLL reset */
	for (uint8_t i = 0; i < PLL_COUNT; ++i) {
		pll_cntl_0.val = 0;
		pll_write_reg(config, i, PLL_CNTL_0_OFFSET, pll_cntl_0.val);
	}

	for (uint8_t i = 0; i < PLL_COUNT; ++i) {
		pll_config_vco(config, i);
	}

	/* Power sequence requires PLLEN get asserted 1us after all inputs are stable.
	 * Wait 5x this time to be convervative */
	k_busy_wait(5);

	/* Power up PLLs */
	pll_cntl_0.f.pd = 1;
	for (uint8_t i = 0; i < PLL_COUNT; ++i) {
		pll_write_reg(config, i, PLL_CNTL_0_OFFSET, pll_cntl_0.val);
	}

	/* Wait for PLLs to lock */
	for (uint8_t i = 0; i < PLL_COUNT; ++i) {
		pll_wait_lock(i);
	}

	/* Setup external postdivs */
	for (uint8_t i = 0; i < PLL_COUNT; ++i) {
		pll_config_ext_postdivs(config, i);
	}

	k_busy_wait(300);

	/* Disable PLL bypass */
	pll_cntl_0.f.bypass = 1;
	for (uint8_t i = 0; i < PLL_COUNT; ++i) {
		pll_write_reg(config, i, PLL_CNTL_0_OFFSET, pll_cntl_0.val);
	}

	k_busy_wait(300);

	enable_clk_counters(config);

	k_spin_unlock(&data->lock, key);
	return 0;
}

static const struct clock_control_driver_api pll_tt_bh_api = {
	.on = pll_tt_bh_on,
	.off = pll_tt_bh_off,
	.async_on = pll_tt_bh_async_on,
	.get_rate = pll_tt_bh_get_rate,
	.get_status = pll_tt_bh_get_status,
	.set_rate = pll_tt_bh_set_rate,
	.configure = pll_tt_bh_configure
};

static struct pll_tt_bh_data pll_tt_bh_data;

static const struct pll_tt_bh_config pll_tt_bh_config = {
	.base = DT_REG_ADDR(DT_NODELABEL(pll)),
	.size = DT_REG_SIZE(DT_NODELABEL(pll)),
};

DEVICE_DT_DEFINE(
	DT_NODELABEL(pll),
	pll_tt_bh_init,
	NULL,
	&pll_tt_bh_data,
	&pll_tt_bh_config,
	POST_KERNEL,
	3,
	&pll_tt_bh_api
);
