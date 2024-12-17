/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TENSTORRENT_FWUPDATE_H_
#define TENSTORRENT_FWUPDATE_H_

#include <stdbool.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include <tenstorrent/tt_boot_fs.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_BOARD_QEMU_X86
/**
 * @brief Set the external flash device and set the passed spi mux (if not null) to allow
 * communication with the spi
 *
 * @param dev The pointer to the external flash device to use.
 * @param mux The spi mux to set and disable on start and completion of the fw update operations.
 *
 * @retval 0 on success, if the spi mux was able to be set
 */
int tt_fwupdate_init(const struct device *dev, struct gpio_dt_spec mux);

/**
 * @brief Called on the completion of the fwupdate operation, disables the spi mux if initialized in
 * `tt_fwupdate_init`
 *
 * @retval 0 on success, if the spi mux was able to be set
 */
int tt_fwupdate_complete(void);
#endif

/**
 * @brief Search for, verify, and apply firmware updates.
 *
 * @note if a firmware update is applied successfully and @p reboot is `true`, then this function
 * does not return.
 *
 * @param tag The tag (name) of the update image in the flash filesystem.
 * @param dry_run Only verify data and operations. Do not write to flash or reboot.
 * @param reboot If true, reboot after the update has been successfully written.
 *
 * @retval 0 on success, if no firmware update is needed.
 * @retval 1 on success, if a firmware update was applied and a reboot is required.
 * @retval -EINVAL if an argument is invalid.
 * @retval -EIO if an I/O error occurs.
 * @retval -ENOENT if the image is invalid an image named @a tag cannot be found.
 * @retval -ENODEV if the current slot cannot be determined or a device is not ready.
 */
int tt_fwupdate(const char *tag, bool dry_run, bool reboot);

/**
 * @brief Confirm that the current firmware has booted successfully
 *
 * @retval 0 on success.
 * @retval -EIO if an I/O erorr occurs.
 */
int tt_fwupdate_confirm(void);

/**
 * @brief Flash the image described by the provided boot filesystem file descriptor.
 *
 * @note This function does not validate the image. Please use @ref tt_fwupdate_validate_image
 * first.
 *
 * @param fd A pointer to the file descriptor for the desired image.
 *
 * @return 0 if the image described by @a fd is valid.
 * @retval -EINVAL if fd is NULL.
 * @retval -ENOENT if the image is invalid.
 * @retval -EIO if an I/O error occurs.
 * @retval -EFBIG if the image is too large to fit in the slot.
 */
int tt_fwupdate_flash_image(const tt_boot_fs_fd *fd);

/**
 * @brief Check if the currently running firmware has been confirmed.
 *
 * @retval 0 if the currently running firmware has not been confirmed.
 * @retval 1 if the currently running firmware has been confirmed.
 * @retval -EIO if an I/O erorr occurs.
 */
int tt_fwupdate_is_confirmed(void);

/**
 * @brief Validate the provided boot filesystem file descriptor.
 *
 * @param fd A pointer to the file descriptor to validate.
 *
 * @retval 0 if @a fd is valid.
 * @retval -EINVAL if fd is NULL.
 * @retval -ENOENT if fd is invalid.
 * @retval -EIO if an I/O error occurs.
 */
int tt_fwupdate_validate_fd(const tt_boot_fs_fd *fd);

/**
 * @brief Validate the image described by the provided boot filesystem file descriptor.
 *
 * @param fd A pointer to the file descriptor for the desired image.
 *
 * @return 0 if the image described by @a fd is valid.
 * @retval -EINVAL if fd is NULL.
 * @retval -ENOENT if the image is invalid.
 * @retval -EIO if an I/O error occurs.
 */
int tt_fwupdate_validate_image(const tt_boot_fs_fd *fd);

#ifdef CONFIG_TT_FWUPDATE_TEST
int tt_fwupdate_create_test_fs(const char *tag);
#endif

#ifdef __cplusplus
}
#endif

#endif
