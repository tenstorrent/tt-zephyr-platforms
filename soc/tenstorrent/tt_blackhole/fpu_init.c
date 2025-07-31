/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Enable FPU for the main thread and the system workqueue thread. */

#include <zephyr/init.h>
#include <zephyr/kernel.h>

static int enable_float_on_main(void)
{
	k_float_enable(k_current_get(), 0);
	return 0;
}

SYS_INIT(enable_float_on_main, APPLICATION, 0);

static void enable_float_on_workqueue(struct k_work *work)
{
	ARG_UNUSED(work);

	k_float_enable(k_current_get(), 0);
}

static K_WORK_DEFINE(enable_float_work, enable_float_on_workqueue);

static int enable_float_sys_work_q(void)
{
	k_work_submit(&enable_float_work);
	return 0;
}

/* The system workqueue is created at POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT.
 * We must enable FPU saving after that, but before any work items that might use the FPU.
 */
SYS_INIT(enable_float_sys_work_q, POST_KERNEL, UTIL_INC(CONFIG_KERNEL_INIT_PRIORITY_DEFAULT));
