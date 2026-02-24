# v18.2.0

We are pleased to announce the release of TT System Firmware firmware version 18.2.0 🥳🎉.

Major enhancements with this release include:

## New Features

* Update Blackhole ERISC FW to v1.4.0
  * Added ETH mailbox with 2 messages
  * ETH msg LINK_STATUS_CHECK: checks for link status
  * ETH msg RELEASE_CORE: Releases control of RISC0 to run function at specified L1 addr
* Virtual UART now enabled by default for Blackhole firmware bundles
  * Creates an in-memory virtual uart for firmware observability and debugging
  * Use `tt-console` to view `printk()` and `LOG_*()` messages from the host

## Stability Improvements

* Update Blackhole ERISC FW to v1.4.0
  * Improve link training sequence for greater success rate on loopback cases
* Fix synchronization issue in BMFW that could result in potential deadlock / failure to enumerate
* Improve SMC I2C recovery function, resulting in reset and re-enumeration success rate of 99.6%
* PCIe Maximum Payload Size (MPS) now set by TT-KMD, improving VM stability

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v80.18.1 release can be found in [v18.2.0 Migration Guide](https://github.com/tenstorrent/tt-system-firmware/tree/main/doc/release/migration-guide-v18.2.0.md).
