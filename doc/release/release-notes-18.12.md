# v18.12.0

> This is a working draft for the up-coming v18.12.0 release.

We are pleased to announce the release of TT Zephyr Platforms firmware version 18.12.0 🥳🎉.

Major enhancements with this release include:

## What's Changed

<!-- Subsections can break down improvements by (area or board) -->
<!-- UL PCIe -->
<!-- UL DDR -->
<!-- UL Ethernet -->
<!-- UL Telemetry -->
<!-- UL Debug / Developer Features -->
<!-- UL Drivers -->
<!-- UL Libraries -->

<!-- Performance Improvements, if applicable -->
### New and Experimental Features

* lib: bh_arc: Add Support for MRISC PHY powerdown/wakeup
* app: dmc: add support for smbus block pcall
* app: smc: add overlay to build DMFW with I2C shell enabled
* lib: bh_arc: Add Power command
* scripts: dump_smc_stack.py
  * dumps the state of the SMC, on demand

<!-- External Project Collaboration Efforts, if applicable -->

### Stability Improvements

* scripts: tooling: tt-console: add option to skip pcie rescan
  * to ensure a smooth transition between TT-KMD 2.3.0 and 2.4.1
* CM2DM robustness improvements
  * detection of 2 identical smbus messages, back to back
  * error detection and logging of smbus messages
* drivers: smbus: stm32: add rate-limited logging of PEC errors
* lib: tenstorrent: bh_arc: add error logging to spi_eeprom
* scripts: blackhole_recovery: add more status messages, increase delays
* scripts: tooling: logging: rework macros and add rate-limited variants
* app: dmc: use fine-grained DMC events
  * a significant improvement over the previous single "wakeup" flag
* boards: tt_blackhole: galaxy: enable aiclk ppm in fw_table
* blobs: update wormhole fw blob
* regulators: fix galaxy serdes programming regression
* ci: build docker containers for ci and recovery
  * containers are now hosted centrally at `ghcr.io/tenstorrent`
* boards: tenstorrent: tt_blackhole: p300: use more conservative tdp limits
* boards: tenstorrent: reduce p300 i2c clock speeds
  * some measured reduction in packet errors at standard speed
* lib: bh_chip: don't disable i2c gate unless we are resetting bh_arc

<!-- Security vulnerabilities fixed? -->

### API Changes

* zephyr: patches: add patch for spi-nor erase-block-size property
* lib: bh_arc: rename `msg_type` to `tt_smc_msg`
  * the corresponding header was changes from `tenstorrent/msg_type.h` to `tenstorrent/smc_msg.h`

<!-- Removed APIs, H3 Deprecated APIs, H3 New APIs, if applicable -->
<!-- New Samples, if applicable -->
<!-- Other Notable Changes, if applicable -->

### Zephyr Version

* manifest: switch to zephyr v4.3.0-pre
  * as the Zephyr v4.3.0 feature freeze approaches (Oct 20th), we have updated to a more recent
    revision
  * sending upstream pull requests to ensure we minimize patches we need to carry in-tree

### Documentatation

* doc: have zversion use the project settings from conf.py
* lib: bh_arc: better documentation
  * add doxygen for several host to smc message types
* doxygen: Add supported boards link
* doc: Add contribution and coding guidelines
* docs: add visualization of CI job status
  * graph of CI history now available on the project's landing page
* doc: add snippet on pulling new code from main
  * `west patch clean; west update; west patch apply`
* lib: bh_arc: document unused commands

<!-- New Boards, if applicable -->

### Removed Boards

* Support for `p100` has been removed
  * `p100` (aka Scrappy) was an internal engineering unit used during initial bringup and during testing
  * Removed support to focus on consumer-facing devices

### Upstream Contributions

* tests: drivers: pwm: pwm_api: clean up platform specific conditionals in testsuite
* drivers: i2c: correct i2c_dw target implementation
* drivers: smbus: stm32: send block write byte count
* tracing: ctf: take IRQ lock before generating timestamp
* drivers: intc_dw: support multiple instancesx
* drivers: smbus: stm32: add block read capabilities
* kernel: events: add "safe" API for `k_event_wait()`

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v18.11.0 release can be found in [v18.12 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-18.12.md).

## Full ChangeLog

The full ChangeLog from the previous v18.11.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v18.11.0...v18.12.0
