.. _tt_z_p_legacy_dmfw_boot:

Legacy DMFW Bootloader Architecture
***********************************

This page describes the architecture of the legacy DMFW bootloader used in
earlier versions of the tt-zephyr-platforms repository, prior to 19.0.0.
This bootloader has since been replaced with the current DMFW Bootloader
(called BL2 during the transition) described in the :ref:`Bootloader
Architecture <tt_z_p_bootloader>` page.

DMFW Legacy Bootloader
======================

The legacy DMFW bootloader is similar to the new bootloader, described
:ref:`here <tt_z_p_bootloader_dmfw>`, but used the `"swap using move"
<https://docs.mcuboot.com/design.html#image-swap-no-scratch>`_ update strategy.
The main difference in the legacy bootloader was that the secondary image
partition was in internal flash as opposed to external SPI flash, since the
latter was not a supported firmware configuration in the Zephyr project at the
time. As such, DMFW was responsible for copying firmware updates from external
spi flash to internal SoC flash.

The legacy DMFW flash layout was defined as follows:

+--------------------+------------------+------------------+------------------+
| Partition Name     | Flash Device     | Offset           | Size             |
+====================+==================+==================+==================+
| MCUBoot            | Internal Flash   | 0x00000000       | 48KiB            |
+--------------------+------------------+------------------+------------------+
| DMFW Active Image  | Internal Flash   | 0x0000C000       | 232KiB           |
+--------------------+------------------+------------------+------------------+
| DMFW Update Image  | Internal Flash   | 0x00046000       | 230KiB           |
+--------------------+------------------+------------------+------------------+
| SPI Flash Update   | SPI Flash        | variable         | variable         |
+--------------------+------------------+------------------+------------------+

For reference, see the `legacy DMFW bootloader devicetree definition
<https://github.com/tenstorrent/tt-zephyr-platforms/blob/v18.12.0/boards/tenstorrent/tt_blackhole/tt_blackhole_dmc.dtsi>`_.

The SPI Flash Update partition contained the new DMFW image, written by
tt-flash. This image was tagged with the tt-boot-fs tag "bmfw", which was used
by the application firmware to identify the image within SPI flash.

DMFW Legacy Update Process
==========================

The legacy DMFW update process involved the following steps:

1. On boot, the DMFW application firmware would read the SPI Flash Update partition to
   check for a new image tagged with "bmfw". If the images was found and the
   CRC did not match the image in "DMFW Active Image" or "DMFW Update Image",
   it would be considered a new image.
2. If a new image was found, the firmware would write the image to the DMFW Update Image
   partition in internal flash.
3. The firmware would then set a flag to indicate that an update is pending and reboot the
   system.
4. On reboot, the MCUBoot bootloader would detect the pending update flag and swap
   the active and update partitions, booting the new DMFW image.

DMFW Legacy State Machine
-------------------------

The DMFW checked the CRC of the "bmfw" image in SPI flash against the images both
internal flash slots to handle the case where an image was tested as an update,
failed to boot, and then was swapped back to slot1 during the revert process.

However, this meant that if an image update sequence like ``A->B->A`` was run,
the second update to image A would be ignored, because the CRC of image A
matched the image already present in the "DMFW Update Image" slot. This is
illustrated in the following state machine diagram:

.. graphviz::

   digraph dmfw_state_machine {
       rankdir=TB
       node [shape=record];
       state0 [label="{DMFW Active Image | DMFW Update Image | SPI Flash Update} | { A | Unknown | A }"];
       state1 [label="{DMFW Active Image | DMFW Update Image | SPI Flash Update} | { B | A | B }"];
       state2 [label="{DMFW Active Image | DMFW Update Image | SPI Flash Update} | { B | A | A }"];

       state0->state1 [label="Image version B is flashed"];
       state1->state2 [label="Image version A is flashed, not copied", fontcolor="red"];
   }

The DMFW would not update to image A from B, because the CRC of image A matched
the image already present in the "DMFW Update Image" slot.

The workaround for this issue is to flash a new image that was different from
both internal flash slots, so the state machine would progress as follows:

.. graphviz::

   digraph dmfw_state_machine_workaround {
       rankdir=TB
       node [shape=record];
       state0 [label="{DMFW Active Image | DMFW Update Image | SPI Flash Update} | { A | Unknown | A }"];
       state1 [label="{DMFW Active Image | DMFW Update Image | SPI Flash Update} | { B | A | B }"];
       state2 [label="{DMFW Active Image | DMFW Update Image | SPI Flash Update} | { C | B | C }"];
       state3 [label="{DMFW Active Image | DMFW Update Image | SPI Flash Update} | { A | B | A }"];

       state0->state1 [label="Image version B is flashed"];
       state1->state2 [label="Image version C is flashed"];
       state2->state3 [label="Image version A is flashed"];
   }


DMFW ROM Update Process
=======================

When performing an update from the legacy bootloader to the current bootloader,
an update package will be written that contains a rom update image tagged as
"bmfw", which the running DMFW can process as an update. The rom update image
will execute, copy the new bootloader to internal flash, and then reboot. After
reboot, the new bootloader will detect that no valid DMFW image is present in
internal flash, and copy the image from external SPI flash to internal flash. It
will then boot the new image as normal.

.. graphviz::
   :caption: DMFW ROM Update Process

   digraph dmfw_rom_update_process {
       node [shape=box];

       "Legacy MCUBoot" -> "Legacy DMFW";
       "Legacy DMFW" -> "ROM Update Image" [label="DMFW requests update to rom update image"];
       "ROM Update Image" -> "New MCUBoot" [label="Rom update image copies new mcuboot and reboots"];
       "New MCUBoot" -> "New DMFW" [label="Update complete"];
   }
