# TT-Zephyr-Platforms v18.3.0

---

We are pleased to announce the release of TT Zephyr Platforms firmware version 18.3.0 🥳🎉.

Major enhancements with this release include:

[comment]: <> (H3 Performance Improvements, if applicable)
[comment]: <> (H3 New and Experimental Features, if applicable)
[comment]: <> (H3 External Project Collaboration Efforts, if applicable)
[comment]: <> (H3 Stability Improvements, if applicable)

### New Features

* DMC now reads and sends power (instead of current) from INA228 device to SMC
  * SMC now uses power reading as input to Total Board Power (TBP) throttler instead of `12 * current`
* DMC support for accessing tca9554a GPIO expanders added

### Stability Improvements

* Add I2C handshake between SMC and DMC FW to ensure that initialization messages are received
* Total Board Power (TBP) throttler parameters have been tuned, and TBP limit is now set in the fwtable to guarantee product definition is followed

[comment]: <> (H1 Security vulnerabilities fixed?)

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v18.2.0 release can be found in [v18.3.0 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-v18.3.0.md).

## API Changes

[comment]: <> (H3 Removed APIs, if applicable)

[comment]: <> (same order for Subsequent H2 sections)
[comment]: <> (UL PCIe)
[comment]: <> (UL DDR)
[comment]: <> (UL Ethernet)
[comment]: <> (UL Telemetry)
[comment]: <> (UL Debug / Developer Features)
[comment]: <> (UL Drivers)
[comment]: <> (UL Libraries)

### Removed APIs
* Telemetry no longer reports TAG_INPUT_CURRENT

[comment]: <> (H3 Deprecated APIs, if applicable)

[comment]: <> (H3 New APIs, if applicable)
### New APIs
* Telemetry now reports TAG_INPUT_POWER to replace TAG_INPUT_CURRENT

[comment]: <> (H2 New Boards, if applicable)

[comment]: <> (H2 New Samples, if applicable)

[comment]: <> (H2 Other Notable Changes, if applicable)
