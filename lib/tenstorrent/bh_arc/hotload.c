/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file hotload.c
 * @brief Hotload handling implementation
 *
 */

#include <tenstorrent/smc_msg.h>
#include <tenstorrent/msgqueue.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control/clock_control_tt_bh.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/linker/devicetree_regions.h>

#define SCRATCH0         0x80030400
#define GLOBAL_RESET     0x80030000
#define ETH_RESET        0x80030008
#define DDR_RESET        0x80030010
#define I2C_CNTL         0x800300f0
#define SPI_CNTL         0x800300f8
#define ARC_MISC_CTRL    0x80030100
#define PLL0_CNTL_OFFSET 0x80020100
#define SCRATCH_MAGIC    0xCAFEBABE

struct arc_vector_table {
	void (*reset)(void); /* Reset vector */
};

Z_GENERIC_SECTION(.hotload.data)
static struct arc_vector_table *vt;

Z_GENERIC_SECTION(.hotload.text)
static void delay(void)
{
	/* Simple delay loop */
	for (volatile int i = 0; i < 100000; i++) {
		__asm__ volatile("nop");
	}
}

/*
 * This function sits within a custom RAM region the hotload cannot overwrite.
 * It MUST not reference external symbols, because when this code
 * runs the remainder of the firmware image will be overwritten
 */

Z_GENERIC_SECTION(.hotload.text)
void wait_jump_request(struct k_timer *timer)
{
	volatile uint32_t *reg;

	ARG_UNUSED(timer);
	/*
	 * Disable interrupts, we will keep them locked
	 * until we jump to new firmware
	 */
	irq_lock();

	/* Indicate to host we are ready to jump */
	reg = (uint32_t *)SCRATCH0;
	*reg = SCRATCH_MAGIC;
	while (*reg == SCRATCH_MAGIC) {
		/* Wait for host to clear the ready signal */
	}

	/*
	 * TODO- it is unclear if we need to reset more here. For now,
	 * these resets are sufficient to get the hotload working with
	 * mission mode CMFW
	 */

	/* Resets must be asserted at slow clock speed. Bypass PLLs */
	for (uint32_t i = 0; i < 5; i++) {
		reg = (uint32_t *)(PLL0_CNTL_OFFSET + (i * 0x100));
		*reg &= ~BIT(4);
		delay();
		reg = (uint32_t *)(PLL0_CNTL_OFFSET + (i * 0x11C));
		*reg = 0x0; /* Disable postdivs */
		delay();
	}

	/* Reset the PCIe/NOC complex */
	reg = (uint32_t *)GLOBAL_RESET;
	/* Clear noc and pcie reset */
	*reg &= ~(BIT(1) | BIT(8));

	/* Reset ETH */
	reg = (uint32_t *)ETH_RESET;
	*reg = 0x0;
	/* Reset DDR */
	reg = (uint32_t *)DDR_RESET;
	*reg = 0x0;
	/* Reset SPI */
	reg = (uint32_t *)SPI_CNTL;
	*reg |= BIT(4); /* SPI reset */
	delay();
	/* Deassert SPI reset */
	*reg &= ~BIT(4);
	/* Assert I2C reset */
	reg = (uint32_t *)I2C_CNTL;
	*reg |= BIT(4); /* I2C reset */
	delay();
	/* Deassert I2C reset */
	*reg &= ~BIT(4);
	/* Jump to the new firmware image */
	vt->reset();
}

K_TIMER_DEFINE(jump_timer, wait_jump_request, NULL);

static uint8_t hotload_handler(const union request *request, struct response *response)
{
	vt = (void *)request->data[1];

	/* Start jump handler */
	k_timer_start(&jump_timer, K_MSEC(100), K_NO_WAIT);

	/* Indicate success to host */
	return 0;
}

REGISTER_MESSAGE(TT_SMC_MSG_HOTLOAD, hotload_handler);

/* Set by linker, */
extern char _hotload_load_addr[];
extern char _hotload_start[];
extern char _hotload_size[];

/* Copy hotload section to correct offset */
static int hotload_init(void)
{
	memcpy((void *)&_hotload_start, (void *)_hotload_load_addr, (size_t)_hotload_size);
	return 0;
}

SYS_INIT(hotload_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
