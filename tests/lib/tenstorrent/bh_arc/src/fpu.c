/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <float.h>

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/arch/arc/v2/aux_regs.h>

/* Verify that FPU state is saved & restored across thread switches.
 * The only FPU-specific state (for our HW config) are ARC auxiliary registers FPU_STATUS and
 * FPU_CTRL.
 */

/* picolib is compiled for soft float so it doesn't use FPU_STATUS or FPU_CTRL.
 * Instead we need custom functions.
 */

#define ARC_ROUNDING_MASK GENMASK(9, 8)

#define ARC_FE_TONEAREST  (1 << 8)
#define ARC_FE_DOWNWARD   (3 << 8)
#define ARC_FE_UPWARD     (2 << 8)
#define ARC_FE_TOWARDZERO (0 << 8)

int arc_fesetround(int round)
{
	uint32_t fpu_ctrl = z_arc_v2_aux_reg_read(_ARC_V2_FPU_CTRL);

	fpu_ctrl &= ~ARC_ROUNDING_MASK;
	fpu_ctrl |= round;

	z_arc_v2_aux_reg_write(_ARC_V2_FPU_CTRL, fpu_ctrl);
	return 0;
}

int arc_fegetround(void)
{
	uint32_t fpu_ctrl = z_arc_v2_aux_reg_read(_ARC_V2_FPU_CTRL);

	return FIELD_GET(ARC_ROUNDING_MASK, fpu_ctrl);
}

#define ARC_FE_INVALID   BIT(0)
#define ARC_FE_DIVBYZERO BIT(1)
#define ARC_FE_OVERFLOW  BIT(2)
#define ARC_FE_UNDERFLOW BIT(3)
#define ARC_FE_INEXACT   BIT(4)

#define ARC_FE_ALL_EXCEPT                                                                          \
	ARC_FE_DIVBYZERO | ARC_FE_INEXACT | ARC_FE_INVALID | ARC_FE_OVERFLOW | ARC_FE_UNDERFLOW

#define ARC_FPU_STATUS_CLEAR_BIT_SHIFT 8

int arc_feclearexcept(int excepts)
{
	/* write-1-to-clear bits for each exception condition at bits 8-12
	 * (when write enable is clear).
	 */
	z_arc_v2_aux_reg_write(_ARC_V2_FPU_STATUS, excepts << ARC_FPU_STATUS_CLEAR_BIT_SHIFT);
	return 0;
}

int arc_fetestexcept(int excepts)
{
	uint32_t fpu_status = z_arc_v2_aux_reg_read(_ARC_V2_FPU_STATUS);

	return fpu_status & excepts;
}

static void set_upward_and_overflow(struct k_work *work)
{
	ARG_UNUSED(work);

	arc_fesetround(ARC_FE_UPWARD);

	volatile float a = FLT_MAX;
	volatile float b = a * a; /* overflow exception */

	ARG_UNUSED(b);
}
static K_WORK_DEFINE(set_upward_and_overflow_work, set_upward_and_overflow);

static void check_upward_and_overflow(struct k_work *work)
{
	ARG_UNUSED(work);

	zassert_equal(arc_fetestexcept(ARC_FE_ALL_EXCEPT), ARC_FE_OVERFLOW);

	volatile float a = 1.0f;
	volatile float b = 3.0f;
	volatile float c = a / b;

	zassert_equal(c, 0.3333333432674407958984375f);
}
static K_WORK_DEFINE(check_upward_and_overflow_work, check_upward_and_overflow);

static struct k_work_sync work_sync;

ZTEST(fpu, test_fpu_state_saving)
{
	/* Assume we're running on main. Verifying that main already has FPU state saving
	 * enabled is part of the test.
	 */

	arc_fesetround(ARC_FE_DOWNWARD);
	arc_feclearexcept(ARC_FE_ALL_EXCEPT);

	volatile float a = 1.0f;
	volatile float b = 0.0f;
	volatile float c = a / b; /* divide by zero exception */

	ARG_UNUSED(c);

	/* run work item to change rounding mode to UPWARD and change exceptions to OVERFLOW */
	k_work_submit(&set_upward_and_overflow_work);
	k_work_flush(&set_upward_and_overflow_work, &work_sync);

	/* verify only divide-by-zero exception is set */
	zassert_equal(arc_fetestexcept(ARC_FE_ALL_EXCEPT), ARC_FE_DIVBYZERO);

	/* verify rounding is still downward */
	volatile float d = 1.0f;
	volatile float e = 3.0f;
	volatile float f = d / e;

	zassert_equal(f, 0.333333313465118408203125f);

	/* run work item to verify rounding mode is still UPWARD and OVERFLOW is still flagged */
	k_work_submit(&check_upward_and_overflow_work);
	k_work_flush(&check_upward_and_overflow_work, &work_sync);

	/* cleanup */
	arc_fesetround(ARC_FE_TONEAREST);
	arc_feclearexcept(ARC_FE_ALL_EXCEPT);
}

ZTEST_SUITE(fpu, NULL, NULL, NULL, NULL, NULL);
