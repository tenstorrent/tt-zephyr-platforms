# v19.8.0

We are pleased to announce the release of TT System Firmware firmware version 19.8.0 🥳🎉.

Major enhancements with this release include:

## Wormhole

### Bug Fixes
- Fix GDDR speed reporting in tt-smi (regression introduced in 19.5.1 / 19.6.0)

### Power & Performance Improvements
- Add support for more idle power saving features
  - GDDR low power mode
  - PCIe switch to Gen1
  - Lower vcore voltage when in aiclk idle

## Blackhole

<!-- Performance Improvements, if applicable -->
<!-- New and Experimental Features, if applicable -->
### New Features

- Added cable fault detection to protect the board when the 12V-2x6 power cable is missing or improperly installed. The DMC detects the cable power limit via GPIO sense pins and writes it to SCRATCH_1 before ARC soft reset. If the SMC reads a power limit of 0, it enters a low-power cable fault mode: all tiles are clock-gated, firmware init is skipped for affected subsystems, and PCIe communication with the host is maintained.
- Added AICLK host frequency arbitration with SMC message handler for dynamic frequency control
- Added cable power limit communication from DMC to SMC via JTAG for real-time power monitoring
- Added adjustable TDP (Thermal Design Power) limit through throttler for improved power management

<!-- External Project Collaboration Efforts, if applicable -->
### Stability Improvements

- Updated Blackhole ERISC FW to v1.10.1, change list from v1.9.0:
  - v1.10.1
    - Fix retraining sequence in LT only mode caused by missing register clear
  - v1.10.0
    - Improve link stability and training reliability for ANLT mode (LT mode)
      - Added LT only training mode without AN, now default over ANLT
    - Fixed bug in Manual EQ sequence that caused reinits to hang
    - Updated ETH mailbox message selection
      - Removed INTERRUPT, CABLE_CHECK, TELEMETRY_EVENT, REMOTE_ALIVE, PORT_MODE messages as they were deemed unneeded
      - Did not re-number messages to avoid updates in higher layer software
- Updated Blackhole MRISC FW to v2.14
  - Fix training instability in revB UBBs
  - Clear EDC error counters after training so they don't get reported in telemetry

### Drivers

- Increased firmware table buffer size
- Enhanced GDDR driver with improved PHY wake-up sequencing and hang detection

### Power Management

- Re-enabled PHY powerdown for MRISC on Galaxy systems
- Implemented cable fault low-power mode that minimizes power draw when 12V-2x6 cable is missing
- Enhanced TDP throttler with configurable power limits

### Testing & CI Improvements

- Enhanced smoke tests with CONFIG_ASSERT=y in nightly CI for improved error detection
- Added end-to-end tests for power management features including TDP limit messaging
- Added unit tests for AICLK PPM, message queue handlers, and EEPROM operations
- Enhanced GDDR status checking in reset tests for better reliability validation
- Added test coverage for firmware bundle diff functionality and tensix disable features

### API Changes

- New throttler count SMC message to display how often each throttler has been activated
  - Provides get, freeze, and clear functionality for each throttler.
  - [Documentation](https://docs.tenstorrent.com/tt-system-firmware/doxygen/structcounter__rqst.html) and [sample usage](https://github.com/tenstorrent/tt-system-firmware/blob/b59acea1d296f959780de5a9ee5be025005b0708/app/smc/pytest/e2e_smoke.py#L462).
- Added extensive Doxygen documentation for SMC message APIs including:
  - ForceVddHandler request structures
  - AICLK host message protocols
  - EEPROM read/write operations
  - Power domain and voltage monitoring messages
  - Temperature sensor reading interfaces
- Enhanced telemetry APIs with GDDR BIST status reporting
- Added cable power limit registers and fault status bits to status register interface

### Scripts & Tooling

- Added comprehensive Wormhole recovery scripts and Docker image with recovery assets
- Enhanced Flash Loader Module (FLM) with:
  - W25Q64JW flash device support with variable address length
  - Status register read/write operations and block protect bit clearing
  - Write enable functionality for global lock/unlock commands
- Added PyOCD utilities integration for improved debugging workflows
- Enhanced device initialization with configurable init/deinit function pointers
- Improved firmware bundle tooling with better diff support for unsigned bootfs entries

<!-- Security vulnerabilities fixed? -->
<!-- Other Notable Changes, if applicable -->
<!-- New Boards, if applicable -->

## Grendel

### New Features

- Added OCCP (Open Chip Communication Protocol) library for standardized communication protocols
- Added TT SMC remoteproc driver for remote processor management and lifecycle control

### Drivers

- Added Grendel SMC DMA driver with scatter/gather transfer support
- Added Grendel SMC PLL clock control driver for dynamic frequency scaling

### Testing & CI Improvements

- Added comprehensive test coverage for new Grendel SMC drivers (DMA and clock control)
- Added initial GitLab CI configuration for Grendel test execution

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v19.7.0 release can be found in [19.8 Migration Guide](https://github.com/tenstorrent/tt-system-firmware/tree/main/doc/release/migration-guide-19.8.md).

## Full ChangeLog

The full ChangeLog from the previous v19.7.0 release can be found at the link below.

https://github.com/tenstorrent/tt-system-firmware/compare/v19.7.0...v19.8.0
