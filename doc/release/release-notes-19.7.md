# v19.7.0

> This is a working draft for the up-coming 19.7.0 release.

We are pleased to announce the release of TT System Firmware firmware version 19.7.0 🥳🎉.

Major enhancements with this release include:

## Wormhole

### New Features
- Add support for BH-style message queues
- Add support for POWER_SETTING message via the message queue
  - Only support Tensix power flag for now

### Stability Improvements
- Avoid attempting to wipe disabled/untrained DRAMs
- Improve DRAM training stability
- Reduce DRAM training time

## Blackhole

### API Changes
- New CMFW message `TT_SMC_MSG_TOGGLE_GDDR_RESET` to reset and retrain a specific GDDR controller (0-7).
  Sample usage can be found in the [pytest file](https://github.com/tenstorrent/tt-system-firmware/tree/main/app/smc/pytest/e2e_smoke.py#L961).

### Developer Features
- DMFW and SMFW now store Zephyr's `APP_VERSION_STRING` and `APP_BUILD_VERSION` binary descriptors, which provide the app version and the FW bundle's git revision respectively. These are printed to the boot banner, and can also be accessed using Zephyr's [`west bindesc` tool](https://docs.zephyrproject.org/latest/services/binary_descriptors/index.html).
- Added DEST register wipe during Tensix initialization, ensuring all non-harvested cores start with zeroed DEST registers at boot. A TRISC wipe firmware is loaded from SPI flash via multicast and executed on TRISC 0 across all cores.

<!-- Subsections can break down improvements by (area or board) -->
<!-- UL PCIe -->
<!-- UL DDR -->
<!-- UL Ethernet -->
<!-- UL Telemetry -->
<!-- UL Debug / Developer Features -->
<!-- UL Drivers -->
<!-- UL Libraries -->

<!-- Performance Improvements, if applicable -->
<!-- New and Experimental Features, if applicable -->
<!-- External Project Collaboration Efforts, if applicable -->

### Stability Improvements

- Updated Blackhole ERISC FW to v1.9.0
  - Embedded erisc fw version at known location (offset 0x188) for SYSIP-190
  - Increased ERISC_CODE region by 4K, taken from ERISC_DATA region
  - Updated chip_info_exchange to be more of a handshake
  - Fixed send telemetry logic bug
  - Fixed bug where is eth_sel unset the core would still attempt SerDes init
  - Reworked SerDes synchronization between 2 ETH cores sharing a SerDes
  - Added per-lane SerDes reset control
    - Reworked port action up/down to properly bring link down
    - Reinit from reset only toggles lane resets instead of full SerDes reset

<!-- Security vulnerabilities fixed? -->
<!-- API Changes, if applicable -->
<!-- Removed APIs, H3 Deprecated APIs, H3 New APIs, if applicable -->
<!-- New Samples, if applicable -->
<!-- Other Notable Changes, if applicable -->
<!-- New Boards, if applicable -->

### System Improvements
- Improved DRAM interface margins on Galaxy Rev C boards

### NOC
- Fixed NOC "logical_id" programming for GDDR ports (this does not functionally impact NOC translation, just how it is reported)

## Grendel

Added a virtual console logging driver, which supports outputting
logs from Zephyr's logging subsystem via the virtual console used in pre-SI
simulation

### Driver Additions

- Enabled driver for CDNS MIPI I3C peripheral

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v19.6.0 release can be found in [19.7 Migration Guide](https://github.com/tenstorrent/tt-system-firmware/tree/main/doc/release/migration-guide-19.7.md).

## Full ChangeLog

The full ChangeLog from the previous v19.6.0 release can be found at the link below.

https://github.com/tenstorrent/tt-system-firmware/compare/v19.6.0...v19.7.0
