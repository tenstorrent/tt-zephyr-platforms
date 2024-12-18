/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/sys/__assert.h>

uint32_t tt_boot_fs_next(uint32_t last_fd_addr)
{
  return (last_fd_addr + sizeof(tt_boot_fs_fd));
}

// Setups hardware abstraction layer (HAL) callbacks, initializes HEAD fd
int tt_boot_fs_mount(tt_boot_fs *tt_boot_fs, tt_boot_fs_read hal_read, tt_boot_fs_write hal_write,
                     tt_boot_fs_erase hal_erase)
{
  tt_boot_fs->hal_spi_read_f = hal_read;
  tt_boot_fs->hal_spi_write_f = hal_write;
  tt_boot_fs->hal_spi_erase_f = hal_erase;

  return TT_BOOT_FS_OK;
}

// Allocates new file descriptor on SPI device
// Writes associated data to correct address on SPI device
int tt_boot_fs_add_file(const tt_boot_fs *tt_boot_fs, tt_boot_fs_fd fd, const uint8_t *image_data_src,
                        bool isFailoverEntry, bool isSecurityBinaryEntry)
{
  uint32_t curr_fd_addr;

  // Failover image has specific file descriptor location (BOOT_START + DESC_REGION_SIZE)
  if (isFailoverEntry) {
    curr_fd_addr = TT_BOOT_FS_FAILOVER_HEAD_ADDR;
  } else if (isSecurityBinaryEntry) {
    curr_fd_addr = TT_BOOT_FS_SECURITY_BINARY_FD_ADDR;
  } else {
    // Regular file descriptor
    tt_boot_fs_fd head = {0};
    curr_fd_addr = TT_BOOT_FS_FD_HEAD_ADDR;

    tt_boot_fs->hal_spi_read_f(TT_BOOT_FS_FD_HEAD_ADDR, sizeof(tt_boot_fs_fd), (uint8_t *)&head);

    // Traverse until we find an invalid file descriptor entry in SPI device array
    while (head.flags.f.invalid == 0) {
      curr_fd_addr = tt_boot_fs_next(curr_fd_addr);
      tt_boot_fs->hal_spi_read_f(curr_fd_addr, sizeof(tt_boot_fs_fd), (uint8_t *)&head);
    }
  }

  tt_boot_fs->hal_spi_write_f(curr_fd_addr, sizeof(tt_boot_fs_fd), (uint8_t *)&fd);

  // Now copy total image size from image_data_src pointer into the SPI device address
  // specified Total image size = image_size + signature_size (security) + padding (future
  // work)
  uint32_t total_image_size = fd.flags.f.image_size + fd.security_flags.f.signature_size;
  tt_boot_fs->hal_spi_write_f(fd.spi_addr, total_image_size, image_data_src);

  return TT_BOOT_FS_OK;
}

uint32_t tt_boot_fs_cksum(uint32_t cksum, const uint8_t *data, size_t num_bytes)
{
  if (num_bytes == 0 || data == NULL) {
    return 0;
  }

  // Always read 1 fewer word, and handle the 4 possible alignment cases outside the loop
  const uint32_t num_dwords = num_bytes / sizeof(uint32_t) - 1;

  uint32_t *data_as_dwords = (uint32_t *)data;
  for (uint32_t i = 0; i < num_dwords; i++) {
    cksum += *data_as_dwords++;
  }

  switch (num_bytes % 4) {
#if 0
      case 3: cksum += *data_as_dwords & 0x000000ff; break;
      case 2: cksum += *data_as_dwords & 0x0000ffff; break;
      case 1: cksum += *data_as_dwords & 0x00ffffff; break;
#endif
  case 0:
    cksum += *data_as_dwords & 0xffffffff;
    break;
  default:
    __ASSERT(false, "size %zu is not a multiple of 4", num_bytes);
    break;
  }

  return cksum;
}
