# TT-Zephyr-Platforms v18.2.0

---

We are pleased to announce the release of TT Zephyr Platforms firmware version 18.2.0 🥳🎉.

Major enhancements with this release include:

[comment]: <> (H3 Performance Improvements, if applicable)
[comment]: <> (H3 New and Experimental Features, if applicable)

### New Features

* Update Blackhole ERISC FW to v1.4.0
  * Added ETH mailbox with 2 messages
  * ETH msg LINK_STATUS_CHECK: checks for link status
  * ETH msg RELEASE_CORE: Releases control of RISC0 to run function at specified L1 addr
* Virtual UART now enabled by default for Blackhole firmware bundles
  * Creates an in-memory virtual uart for firmware observability and debugging
  * Use `tt-console` to view `printk()` and `LOG_*()` messages from the host

[comment]: <> (H3 External Project Collaboration Efforts, if applicable)
[comment]: <> (H3 Stability Improvements, if applicable)

### Stability Improvements

* Update Blackhole ERISC FW to v1.4.0
  * Improve link training sequence for greater success rate on loopback cases
* Fix synchronization issue in BMFW that could result in potential deadlock / failure to enumerate
* Improve SMC I2C recovery function, resulting in reset and re-enumeration success rate of 99.6%
* PCIe Maximum Payload Size (MPS) now set by TT-KMD, improving VM stability

[comment]: <> (H1 Security vulnerabilities fixed?)

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v80.18.1 release can be found in [v18.2.0 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-v18.2.0.md).

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

[comment]: <> (H3 Deprecated APIs, if applicable)

[comment]: <> (H3 New APIs, if applicable)

[comment]: <> (H2 New Boards, if applicable)

[comment]: <> (H2 New Samples, if applicable)

[comment]: <> (H2 Other Notable Changes, if applicable)
