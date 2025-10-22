# v19.0.0

## Migration Guide

This document lists recommended and required changes for those migrating from the previous v18.12.0 firmware release to the new v19.0.0 firmware release.

### TT-Flash Update

As part of the update to firmware release 19.0, the firmware bundle revision has
been incremented to 2.0.0. Users *must* update to tt-flash 3.4.7 or later to
install this firmware update, as the update process has changed.

To update to tt-flash, run `pip install 'tt-flash>=3.4.7'`

### Firmware Update Process

As part of the update to firmware 19.0, the DMFW bootloader will be updated.
This update process takes longer than a standard update, so the reset phase
for updates from 18.x -> 19.x within tt-flash will take 60 seconds versus 20
seconds for a standard update. The update process will look like so:

```
tt-flash fw_pack-19.0.0.fwbundle
Stage: SETUP
        Searching for default sys-config path
        Checking /etc/tenstorrent/config.json: not found
        Checking ~/.config/tenstorrent/config.json: not found

        Could not find config in default search locations, if you need it, either pass it in explicitly or generate one
        Warning: continuing without sys-config, galaxy systems will not be reset
Stage: DETECT
Stage: FLASH
        Sub Stage: VERIFY
                Verifying fw-package can be flashed: complete
                Verifying Blackhole[0] can be flashed
        Stage: FLASH
                Sub Stage FLASH Step 1: Blackhole[0]
                        Detected major version upgrade from (18, 12, 0, 0) to (19, 0, 0, 1)
                        ROM version is: (18, 12, 0, 0). tt-flash version is: (19, 0, 0, 1)
                        FW bundle version > ROM version. ROM will now be updated.
                Sub Stage FLASH Step 2: Blackhole[0] {p150a}
                        Writing new firmware... SUCCESS
                        Firmware verification... SUCCESS
Stage: RESET
                Detected update across major version, will wait 60 seconds for m3 to boot after reset
 Starting reset on devices at PCI indices: 0
 Waiting for 60 seconds for potential hotplug removal.
 Waiting for devices to reappear on pci bus...
 Reset successfully completed for device at PCI index 0.
 Finishing reset on devices at PCI indices: 0
FLASH SUCCESS
```
