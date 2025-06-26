/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

/* Provides override hooks within mcuboot for specific operations */

#include <assert.h>
#include <zephyr/kernel.h>
#include "bootutil/image.h"
#include "bootutil/bootutil.h"
#include "bootutil/fault_injection_hardening.h"
#include "flash_map_backend/flash_map_backend.h"

/*
 * Hook called to validate image. We override this to skip signature validation,
 * as otherwise we will miss the PCIe enumeration deadline (and this part isn't
 * secure anyway).
 * @retval FIH_SUCCESS: image is valid,
 *         FIH_FAILURE: image is invalid,
 *         fih encoded BOOT_HOOK_REGULAR if hook not implemented for
 *         the image-slot.
 */
fih_ret boot_image_check_hook(int img_index, int slot)
{
	/* Skip all validation, launch image */
	FIH_RET(FIH_SUCCESS);
}

/*
 * The remaining hooks are needed for linking, but we just return BOOT_HOOK_REGULAR
 * to make mcuboot use the normal routine
 */

/* @retval 0: header was read/populated
 *         FIH_FAILURE: image is invalid,
 *         BOOT_HOOK_REGULAR if hook not implemented for the image-slot,
 *         othervise an error-code value.
 */
int boot_read_image_header_hook(int img_index, int slot, struct image_header *img_hed)
{
	return FIH_BOOT_HOOK_REGULAR;
}

int boot_perform_update_hook(int img_index, struct image_header *img_head,
			     const struct flash_area *area)
{
	return FIH_BOOT_HOOK_REGULAR;
}

int boot_read_swap_state_primary_slot_hook(int image_index, struct boot_swap_state *state)
{
	return FIH_BOOT_HOOK_REGULAR;
}

int boot_copy_region_post_hook(int img_index, const struct flash_area *area, size_t size)
{
	return 0;
}

int boot_serial_uploaded_hook(int img_index, const struct flash_area *area, size_t size)
{
	return 0;
}

int boot_img_install_stat_hook(int image_index, int slot, int *img_install_stat)
{
	return FIH_BOOT_HOOK_REGULAR;
}
