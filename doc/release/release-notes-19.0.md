# TT-Zephyr-Platforms v19.0.0

> [!NOTE]
> This is a working draft for the up-coming v19.0.0 release.

[comment]: <> (We are pleased to announce the release of TT Zephyr Platforms firmware version 19.0.0 🥳🎉.)

Major enhancements with this release include:

### Performance Improvements

* Load spi flash images using temporary buffer rather than large statically allocated SRAM buffer

[comment]: <> (H3 New and Experimental Features, if applicable)
[comment]: <> (H3 External Project Collaboration Efforts, if applicable)

### Stability Improvements

* Update Blackhole ERISC FW to v1.5.0
  * Updated some eth training sequencing to help with Galaxy UBB products
  * Added CALL_ACK postcode in eth msg mailboxes to show message has been read and is being processed
  * Added fence instructions in eth msg mailboxes to invalidate L1 cache when polling
  * Changed default training mode to AUTO mode from ANLT, will result in manual eq on Galaxy QSFP ports
  * ETH msg FEATURE_ENABLE: allows for enable/disablement of eth fw features

[comment]: <> (H1 Security vulnerabilities fixed?)

[comment]: <> (H2 API Changes, if applicable)

[comment]: <> (H3 Removed APIs, H3 Deprecated APIs, H3 New APIs, if applicable)

[comment]: <> (UL PCIe)
[comment]: <> (UL DDR)
[comment]: <> (UL Ethernet)
[comment]: <> (UL Telemetry)
[comment]: <> (UL Debug / Developer Features)
[comment]: <> (UL Drivers)
[comment]: <> (UL Libraries)

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

[comment]: <> (H2 New Boards, if applicable)

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v18.8.0 release can be found in [v19.0 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-19.0.md).

## Full ChangeLog

The full ChangeLog from the previous v18.8.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v18.8.0...v19.0.0
