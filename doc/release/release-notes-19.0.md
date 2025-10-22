# v19.0.0

We are pleased to announce the release of TT Zephyr Platforms firmware version 19.0.0 🥳🎉.

> [!IMPORTANT]
> As this is a major release, downgrades below this release (19.x -> 18.x)
> are not supported. This release also requires tt-flash 3.4.7 or later to
> flash onto a card.

Major enhancements with this release include:

- New bootloader scheme for CMFW and DMFW. This will improve update reliability
  for both firmware binaries
- If firmware flash fails, CMFW will now revert to recovery mode. From this mode
  tt-flash can be used to restore a working firmware.

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

* Update Blackhole ERISC FW to v1.7.0
  * ETH msg PORT_RETRAIN: force a link to retrain
  * ETH msg PORT_REINIT: asks a failed port to redo initialization
  * ETH msg PORT_LOOPBACK: allows putting the port in internal or external loopback
  * ETH msg INTERRUPT: enables or disables interrupts to the ERISC
  * ETH msg PORT_ACTION: force the link to be up or down via the MAC
  * ETH msg CABLE_CHECK: checks whether a cables exists or not
  * ETH msg TELEMETRY_EVENT: handles specific telemetry exchange events over the link
  * ETH msg REMOTE_ALIVE: send packet to check if remote side is alive
  * ETH msg PORT_SPEED: re-initializes the port to a different speed

<!-- External Project Collaboration Efforts, if applicable -->

### Stability Improvements

* Update Blackhole ERISC FW to v1.7.0
  * Fix snapshot reading bug in eth_runtime where the upper 32 bits of a preceding metric read is picked up by the following metric read
  * Remove interrupt enablement as current implementation can cause infinite loops
  * Changed logical_eth_id calculation using new enabled_eth param to address SYS-2064
  * Added ASIC ID in chip_info and param table to address SYS-2065
  * Changed manual EQ TX-FIRs for ASIC 8 Retimer ports to address SYS-2096
  * Only trigger retraining if check_link_up polls link down for 5ms
  * Removed BIST check in training sequence, improves stability a bit
  * Send chip_info packet on retrain completion, which along with BIST disabled allows for a single chip with an active link to be reset and allow the link come back up
  * Set manual TX FIR parameters for warp cable connections on P300 to 1/3/4/45/2 for PCB-1997
  * increase stack size to 2048 for SYS-2266
  * inline icache flush function for SYS-2267
  * Fix for reset skew where one tt-smi reset should make other side up
  * Added interrupt enablement again, controlled via INTERRUPT_CHECK feature enable flag
  * Moved auto retraining outside of link_status_check into its own link check state machine, controlled via DYNAMIC_LINK_STATE_CHECK feature enable flag
  * Added link flap check based on resend and un-cor words
  * Added eth_reinit state machine to handle fail case when port is up
* PVT Sensor
  * correct PVT RTIO buffer size and frame count for decode
* Update Wormhole FW blob
  * ERISC 6.7.2.0
  * Add support to trigger retraining on failed training ethernet ports

<!-- Security vulnerabilities fixed? -->
<!-- API Changes, if applicable -->
<!-- Removed APIs, H3 Deprecated APIs, H3 New APIs, if applicable -->
<!-- New Samples, if applicable -->
<!-- Other Notable Changes, if applicable -->
<!-- New Boards, if applicable -->

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v18.12.0 release can be found in [v19.0 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-19.0.md).

## Full ChangeLog

The full ChangeLog from the previous v18.12.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v18.12.0...v19.0.0
