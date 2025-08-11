# Migration Guide: TT-Zephyr-Platforms v18.8.0

> [!NOTE]
> This is a working draft for the up-coming v18.8.0 release.
This document lists recommended and required changes for those migrating from the previous v18.7.0 firmware release to the new v18.8.0 firmware release.

[comment]: <> (UL by area, indented as necessary)

* Update `luwen`, `tt-smi` and `tt-flash` to support more firmware telemetry values being added.
  * `luwen` >= 0.7.2
  * `tt-smi` >= v3.0.22
  * `tt-flash` >= v3.3.4
* For P300a boards, running `./scripts/rescan-pcie.sh` will crash the kernel. To restore PCIe enumeration:
  1. Power cycle the board
  2. Run `west flash`
  3. Run `sudo reboot`
* For P300c boards, if PCIe enumeration does not work after flashing DMC:
  1. Use bootstrap + JTAG to flash the fwbundle for each chip
  2. Power cycle the board
