# v18.11.0

We are pleased to announce the release of TT Zephyr Platforms firmware version 18.11.0 🥳🎉.

Major enhancements with this release include:

## What's Changed

<!-- H3 Performance Improvements, if applicable -->

### Performance Improvements

* soc: tt_blackhole: enable hardware floating-point in SMC FW

<!-- H3 New and Experimental Features, if applicable -->
<!-- H3 External Project Collaboration Efforts, if applicable -->

<!-- H3 Stability Improvements, if applicable -->

### Stability Improvements

* boards: tt_blackhole: p150c: correct tdp_limit & tdc_limits
* scripts: recover-blackhole: add recovery bundle scripting in-tree
* patches: Fix smbus PEC correction patch
* ci: workflows: hardware-long: update metal container version and test p300a
* app: dmc: Move MFD, PWM and sensor configs to prj.conf
* scripts: tooling: vuart: report error if card reads as `0xffff_ffff`

<!-- H1 Security vulnerabilities fixed? -->

<!-- H2 API Changes, if applicable -->

<!-- H3 Removed APIs, H3 Deprecated APIs, H3 New APIs, if applicable -->

<!-- UL PCIe -->
<!-- UL DDR -->
<!-- UL Ethernet -->
<!-- UL Telemetry -->
<!-- UL Debug / Developer Features -->

### Debug / Developer Features

* `tt-zephyr-platforms`documentation is now available online at https://docs.tenstorrent.com/tt-zephyr-platforms 🙌🪁
  * doc: getting_started: move contents from README.md to getting started guide, and publish to HTML
  * doc: develop: add documentation for tracing
* ci: build-native: publish tt-console and tt-tracing artifacts
  * previously, users needed to build these binaries themselves with `make -C scripts/tooling`. Now they are built on every commit.
* ci: release: ensure build artifacts for all board revisions are in zip
* scripts: tt_boot_fs: ls: add hexdump functionality when verbose >= 2
  * this allows us to inspect all contents of TT Boot FS .bin files without breaking out a separate hex editor
* bh_arc: tt_shell: add shell support
  * a common location for custom Tenstorrent shell commands
  * print telemetry data with `telem` sub-command
  * read (and write) asic state with `asic_state` sub-command
* app: dmc: enable logging via DMC SMBUS path
  * first logs to a local ringbuffer, and then data is sent over smbus from DMC to SMC
  * users should now be able to view DMC logs with `tt-console -c 2`

<!-- UL Drivers -->

### Drivers

* drivers: sensor: pvt: integrate pvt driver, add tolerance in tests, read efused temperature calibration
* drivers: smbus: add platform-independent `zephyr,smbus-target` driver (should be upstreamed shortly)

<!-- UL Libraries -->

<!-- H2 New Samples, if applicable -->

<!-- UL PCIe -->
<!-- UL DDR -->
<!-- UL Ethernet -->
<!-- UL Telemetry -->
<!-- UL Debug / Developer Features -->
<!-- UL Drivers -->
<!-- UL Libraries -->

<!-- H2 Other Notable Changes, if applicable -->

<!-- UL PCIe -->
<!-- UL DDR -->
<!-- UL Ethernet -->
<!-- UL Telemetry -->
<!-- UL Debug / Developer Features -->
<!-- UL Drivers -->
<!-- UL Libraries -->

<!-- H2 New Boards, if applicable -->

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v18.10.0 release can be found in [v18.11 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-18.11.md).

## Full ChangeLog

The full ChangeLog from the previous v18.10.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v18.10.0...v18.11.0
