Overview
********

This application is used to update the ROM on the DMC for blackhole PCIe cards.
Initial firmware releases for blackhole cards used an update mechanism that
required two phases- first the update image was written to external SPI,
then the DMC would copy the image into internal flash an run a standard
MCUBoot update.

Newer firmware releases support a single phase update where the image is
copied directly from SPI to internal flash. To enable this, the bootloader
on the DMC must be updated to a new build supporting this layout.

This application is used to perform the DMC bootloader update.

Update Process
**************

This application ships as a standard DMFW update, tagged with the legacy
tt-boot-fs entry "bmfw". When it executes, it will:

1. write the new mcuboot bootloader image to internal flash
2. mark the new DMFW image as pending for an update
3. reboot the DMC
4. on reboot, the new bootloader will run and complete the DMFW update

Rollback
********

Once this application has been run, the DMC will be running the new bootloader.
Rollback to the old bootloader is not supported without a debug probe to
directly reprogram the internal flash.
