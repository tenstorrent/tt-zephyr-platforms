/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <stdint.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log.h>
#include <kernel_arch_func.h>

LOG_MODULE_REGISTER(soc, CONFIG_LOG_DEFAULT_LEVEL);

static inline void delay_spin(uint32_t count)
{
	volatile uint32_t i = count;

	while (i--) {
		/* Spin */
	}
}

#define PANIC_REASON_MAGIC 0xBADC0DE0
#define REASON_K_PANIC     0x1
#define REASON_ARC_RESET   0x2

#define ARC_RESET_ADDR 0x80000000

/*
 * After panic the arc can't write to scratch registers. Write to ICCM
 * to indicate we have panicked.
 */
#define ARC_PANIC(reason, blink)                                                                   \
	do {                                                                                       \
		sys_write32(PANIC_REASON_MAGIC | (reason), 0x0);                                   \
		sys_write32(blink, 0x4);                                                           \
	} while (0)

/*
 * Override the default kernel panic handler. We want to also
 * dump the BLINK register value.
 */
__weak void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);

	ARC_PANIC(REASON_K_PANIC, esf->blink);

	LOG_PANIC();
	LOG_ERR("Halting system");
	k_fatal_halt(reason);
	CODE_UNREACHABLE;
}

/* This function is a custom hook we run during panic */
static void arc_panic(void)
{
	uint32_t blink;

	/* Get the BLINK register */
	__asm__ volatile("mov %0, blink" : "=r"(blink));

	ARC_PANIC(REASON_K_PANIC, blink);
	while (1) {
		/* Spin */
	}
}

void soc_early_init_hook(void)
{
	/* Set the reset vector to arc_panic. */
	sys_write32((uint32_t)&arc_panic, ARC_RESET_ADDR);
	/* Clear ICCM registers */
	sys_write32(0, 0x0);
	sys_write32(0, 0x4);

	if (IS_ENABLED(CONFIG_I2C)) {
		uint32_t reg;
		/* Manually toggle I2C reset control bits */
		volatile uint32_t *RESET_UNIT_I2C_CNTL = (uint32_t *)0x800300F0;

		reg = *RESET_UNIT_I2C_CNTL;
		/* Disable I2C controllers and set reset bit */
		*RESET_UNIT_I2C_CNTL = BIT(4);
		delay_spin(1000);
		/* Clear reset bit */
		*RESET_UNIT_I2C_CNTL = (reg & ~BIT(4));
	}
	if (DT_HAS_COMPAT_STATUS_OKAY(snps_designware_ssi) && IS_ENABLED(CONFIG_MSPI)) {
		/* Manually toggle the SPI reset control bits */
		volatile uint32_t *RESET_UNIT_SPI_CNTL = (uint32_t *)0x800300F8;
		*RESET_UNIT_SPI_CNTL |= BIT(4);
		/* Delay a few cycles- pre kernel so just use a spin loop */
		delay_spin(1000);
		*RESET_UNIT_SPI_CNTL &= ~BIT(4);
		/* Enable the SPI */
		*RESET_UNIT_SPI_CNTL |= BIT(0);
		/* Disable DDR mode */
		*RESET_UNIT_SPI_CNTL &= ~BIT(1);
	}
}
