# TT-Zephyr-Platforms v18.10.0

> [!NOTE]
> This is a working draft for the up-coming v18.10.0 release.

[comment]: <> (We are pleased to announce the release of TT Zephyr Platforms firmware version 18.10.0 🥳🎉.)

Major enhancements with this release include:

[comment]: <> (H3 Performance Improvements, if applicable)
[comment]: <> (H3 New and Experimental Features, if applicable)
[comment]: <> (H3 External Project Collaboration Efforts, if applicable)

### Stability Improvements

* Update Blackhole ERISC FW to v1.6.0
  * Added eth_flush_icache to flush instruction cache for SYS-1944 - function is 2048 NOP instructions unrolled
  * New function pointer in eth_api_table: eth_flush_icache_ptr for aforementioned eth_flush_icache
  * Increased eth code size allocation by 8KB, code now starts at 0x70000
  * Enhanced training flow and retrain logic, links should now more reliably train up
  * Enabled interrupts in erisc fw: now snapshots and clears
  * Added live retraining when check_link_status() detects link down
  * ETH msg LINK_UP_CHECK: fast check to only update rx_link_up field of eth_live_status
* Setup FW table to lower ETH train speeds for Galaxy products to 200G, other products stay 400G

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

An overview of required and recommended changes to make when migrating from the previous v18.9.0 release can be found in [v18.10 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-18.10.md).

## Full ChangeLog

The full ChangeLog from the previous v18.9.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v18.9.0...v18.10.0
