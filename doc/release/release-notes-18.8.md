# TT-Zephyr-Platforms v18.8.1

This is a patch release for the previous 18.8.0 release with the following issues addressed.

* bh_arc: support the `telem_read` and `telem_write` SMBUS messages
* bh_arc: correctly calculate PEC calculation for block-write-block-read operations

# TT-Zephyr-Platforms v18.8.0

We are pleased to announce the release of TT Zephyr Platforms firmware version 18.8.0 🥳🎉.

Major enhancements with this release include:

[comment]: <> (H3 Performance Improvements, if applicable)
[comment]: <> (H3 New and Experimental Features, if applicable)
[comment]: <> (H3 External Project Collaboration Efforts, if applicable)

### Stability Improvements

* Update Blackhole MRISC FW to v2.9
  * Modified Tuning setting for BH Galaxy cards
    * Pull in changes from P300 Learning: dram_ocd_pulldown_offset increased to “3”
    * Adjust CA delay from “8” to “0” (Using the default values that are already used in other projects like P100, P150 and P300).
    * Removed bottom DRAM to train CA bus (Using the default values that are already used in other projects like P100, P150 and P300).
* The `tenstorrent,bh-clock-control` driver has seen some improvements
  * Driver is used by default and initialized via Zephyr's init system, like all other devices in the driver model
  * Fixed a bug where the AICLK frequency was set to 1/4 of the expected value by correctly accounting for postdiv
  * Read AICLK limits from fwtable instead of devicetree
* The `tenstorrent,bh-fwtable` driver now deterministically loads tables on initialization
* Telemetry now displays `AICLK_MAX`, `VDD_LIMIT`, `THM_LIMIT` and `TDC_LIMIT` via `tt-smi`.
* Added regulator config values for p300 and galaxy
* `tt-console`
  * Support rescan via `ioctl()`
  * Fixed a bug to account for rescanning from within a container returning `-ENXIO` instead of the expected `-ENOENT`
  * Do not return with an error if interrupted by a signal (`SIGINT` / `Ctrl+C`)
* Added SMBus command to update ARC state, along with `native_sim`-based testsuite
* P300
  * Normalize `p300a`, `p300b`, and `p300c` to be like regular board variants
  * Add a GPIO spec to Devicetree to for the P300 JTAG mux
  * Fix fan control for p300

[comment]: <> (H1 Security vulnerabilities fixed?)

[comment]: <> (H2 API Changes, if applicable)

### New APIs

[comment]: <> (UL PCIe)
[comment]: <> (UL DDR)
[comment]: <> (UL Ethernet)
[comment]: <> (UL Telemetry)
[comment]: <> (UL Debug / Developer Features)
[comment]: <> (UL Drivers)

### Removed APIs

#### Drivers

* Removed the forked stm32 i2c driver
* Removed the forked stm32 smbus driver

#### Libraries

* TT Boot FS
  * Two new API calls; `ls` to list files in the filesystem, and a second to read an individual file descriptor by tag
  * list files on a filesystem on `dev`: `int tt_boot_fs_ls(const struct device *dev, tt_boot_fs_fd *fds, size_t nfds, size_t offset);`
  * look up a single file on `dev`: `int tt_boot_fs_ls(const struct device *dev, tt_boot_fs_fd *fds, size_t nfds, size_t offset);`
  * The filesystem is now fully specified via devicetree, removing the need for several redundant YAML files

[comment]: <> (H2 New Samples, if applicable)

[comment]: <> (UL PCIe)
[comment]: <> (UL DDR)
[comment]: <> (UL Ethernet)
[comment]: <> (UL Telemetry)
[comment]: <> (UL Debug / Developer Features)
[comment]: <> (UL Drivers)
[comment]: <> (UL Libraries)

[comment]: <> (H2 Other Notable Changes, if applicable)

[comment]: <> (UL PCIe)
[comment]: <> (UL DDR)
[comment]: <> (UL Ethernet)
[comment]: <> (UL Telemetry)
[comment]: <> (UL Debug / Developer Features)
[comment]: <> (UL Drivers)
[comment]: <> (UL Libraries)

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v18.7.0 release can be found in [v18.8 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-18.8.md).

## Full ChangeLog

The full ChangeLog from the previous v18.7.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v18.7.0...v18.8.1
