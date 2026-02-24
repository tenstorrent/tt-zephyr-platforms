.. _tt_z_p_bootloader:

Bootloader Architecture
***********************

The CMFW and DMFW both utilize `MCUBoot
<https://www.trustedfirmware.org/projects/mcuboot/index.html>`_ as a bootloader.
MCUBoot is an open-source bootloader commonly used in Zephyr-based systems. The
CMFW boots in ram-load mode, while the DMFW boots from internal flash with a
dual-image strategy. The bootloader implementations are described in detail
below.

DMFW Bootloader
===============

The DMFW bootloader is a standard MCUBoot implementation, configured to support
dual-image booting. The bootloader utilized the "swap using offset" update
strategy, which is documented in the `MCUBoot swap documentation
<https://docs.mcuboot.com/design.html#image-swap-offset-no-scratch>`_.
The primary image partition is stored in the internal STM32 flash, while the
secondary image partition is stored in an external SPI flash chip. This
maximizes the available space for the DMFW firmware image.

The DMFW flash partitions are defined as follows:

+-----------------------+------------------+------------------+------------------+
| Partition Name        | Flash Device     | Offset           | Size             |
+=======================+==================+==================+==================+
| MCUBoot               | Internal Flash   | 0x00000000       | 64KiB            |
+-----------------------+------------------+------------------+------------------+
| DMFW Primary Image    | Internal Flash   | 0x00010000       | 448KiB           |
+-----------------------+------------------+------------------+------------------+
| DMFW Update Image     | SPI Flash        | 0x0022d000       | 452KiB           |
+-----------------------+------------------+------------------+------------------+

For reference, see the `DMFW bootloader devicetree definition
<https://github.com/tenstorrent/tt-system-firmware/blob/main/boards/
tenstorrent/tt_blackhole/tt_blackhole_dmc.dtsi>`_.

DMFW Boot Process
-----------------

After the STM32 comes out of reset, MCUBoot will execute. MCUBoot checks the
image header in the primary partition to determine if a valid image is present,
and validates that the image SHA sum matches the expected value. If the image is
valid, MCUBoot will boot the image. Otherwise the boot process will fail.

DMFW Update Process
-------------------

Firmware images are written to the SPI flash, as the internal STM32 flash is not
directly writable by tt-flash. The DMFW image within the firmware update package
is defined with a MCUBoot trailer that indicates it is a valid image, but not
yet confirmed. After tt-flash completes execution and instructs the STM32 to
reboot the bootloader will detect the new image and perform a swap operation,
copying the new image from SPI flash to internal STM32 flash. Once the new image
is successfully copied, the bootloader will boot the new image. The DMFW
firmware is responsible for confirming the new image after boot, by calling the
appropriate MCUBoot API. If the image is not confirmed, the bootloader will
revert to the previous image on the next reboot.

.. graphviz::
   :caption: DMFW Update Process

   digraph dmfw_update_process {
       rankdir=LR
       node [shape=box];

       MCUBoot -> "Primary Image" [label="No update pending"];
       MCUBoot -> "Swap Update Image with Primary Image" [label="Update pending"];
       "Swap Update Image with Primary Image" -> "Primary Image";
       "Primary Image" -> "Primary Image" [label="Mark Confirmed"]
   }

CMFW Bootloader
===============

The CMFW bootloader utilizes MCUBoot in the "ram load with revert" mode, which
is documented in the `MCUBoot ramload documentation
<https://docs.mcuboot.com/design.html#ram-load>`_. In this mode, MCUBoot loads
the firmware image from flash to a predefined RAM location, and then jumps to the
RAM location to execute the image. MCUBoot selects the image with the highest
valid version number as the next image to boot. For this reason, the CMFW
boot scheme is defined with a "mission mode" firmware image which will be updated
in place, and a "recovery mode" firmware image which is not updated during normal
operation. If the mission mode image fails to boot, MCUBoot will revert to the
recovery mode image on the next reboot.

The CMFW flash partitions are defined as follows:

+-------------------------+------------------+------------------+------------------+
| Partition Name          | Flash Device     | Offset           | Size             |
+=========================+==================+==================+==================+
| MCUBoot                 | SPI Flash        | 0x00014000       | 128KiB           |
+-------------------------+------------------+------------------+------------------+
| CMFW Recovery Image     | SPI Flash        | 0x00034000       | 512KiB           |
+-------------------------+------------------+------------------+------------------+
| MCUBoot Fallback Image  | SPI Flash        | 0x000B4000       | 128KiB           |
+-------------------------+------------------+------------------+------------------+
| CMFW Mission Image      | SPI Flash        | 0x0029E000       | 512KiB           |
+-------------------------+------------------+------------------+------------------+

For reference, see the `CMFW bootloader devicetree definition
<https://github.com/tenstorrent/tt-system-firmware/blob/main/boards/
tenstorrent/tt_blackhole/tt_blackhole_fixed_partitions.dtsi>`_.

CMFW Boot Process
-----------------

The Blackhole ASIC SMC includes a ROM bootloader that executes on reset. The ROM
bootloader is responsible for loading the firmware image from SPI flash for
execution. It uses the tt-boot-fs file descriptor table stored at the start of
SPI flash, and will perform the following steps on each entry:

1. Check that the entry crc matches the expected value
2. If so, load the entry to the address in ARC CSM or ICCM specified in the
   entry header
3. If the entry is marked as executable, jump to the entry load address
4. If execution returns, continue the above sequence with the next entry

If step 1 fails for any entry, the ROM bootloader will jump to the fallback
descriptor entry at ``0x4000``. This offset is outside the tt-boot-fs table,
and is reserved for the fallback image. The fallback image will then be loaded
and executed.

The CMFW flash partitions are defined such that MCUBoot will execute as the
first entry in the tt-boot-fs table, and will also execute as the recovery
image. This way a damaged tt-boot-fs table will not prevent boot.

Once MCUBoot is executing, it will select the mission mode image as the next
image to boot, as it always has a higher version number than the recovery image.
MCUBoot will only launch the recovery image if the mission mode image header
is invalid, or if the mission mode image fails to boot and MCUBoot reverts
to the recovery image.

CMFW Update Process
-------------------

Firmware images are written to the SPI flash by tt-flash. The mission mode
image within the firmware update package is defined with a MCUBoot trailer that
indicates it is a valid image, but not yet confirmed. After tt-flash completes
execution and instructs the Blackhole ASIC to reboot, the ROM bootloader will
execute MCUBoot, which will load and boot the new mission mode image. The CMFW
firmware is responsible for confirming the new image after boot, by calling the
appropriate MCUBoot API. If the image is not confirmed, MCUBoot will erase the
invalid image and select the recovery image on the next reboot.

.. graphviz::
   :caption: CMFW Boot Process

   digraph cmfw_boot_process {
       rankdir=LR
       node [shape=box];

       "ROM Bootloader" -> MCUBoot [label="Load and Execute"];
       "ROM Bootloader" -> "MCUBoot Fallback Copy" [label="Fallback"];
       MCUBoot -> "Select Boot Image";
       "MCUBoot Fallback Copy" -> "Select Boot Image";
       "Select Boot Image" -> "Mission Image" [label="Boot if present"];
       "Mission Image" -> "Mission Image" [label="Mark Confirmed"];
       "Select Boot Image" -> "Recovery Image" [label="Fallback"];
   }

Host Flash Access
=================

The host can access external SPI flash via ARC message commands, which are
handled by the CMFW. The CMFW exposes commands to read and write flash, where
the write command will automatically handle erasing the necessary flash sectors
before writing. The host can use these commands to read or write any region of
the external SPI flash, including the bootloader and firmware image partitions.

The tt-flash utility uses pyluwen to write firmware images to SPI flash, which
implements these ARC message commands. The relevant code for tt-flash can be
found `in the tt-flash repository
<https://github.com/tenstorrent/tt-flash/blob/
e7b78ab39d433d9ecdaffc01bdb8e0d53ec19255/tt_flash/flash.py#L486>`_,
and the pyluwen ARC message commands can be found `in the pyluwen repository
<https://github.com/tenstorrent/luwen/blob/
b9ac56c9ac3cd2a6ac100aeb060fd0988b46d8f5/crates/luwen-if/src/chip/blackhole.rs#L344>`_.
