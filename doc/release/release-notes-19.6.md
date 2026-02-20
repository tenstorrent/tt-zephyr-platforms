# v19.6.0

We are pleased to announce the release of TT Zephyr Platforms firmware version 19.6.0 🥳🎉.

Major enhancements with this release include:

## Wormhole Changes

### New Features
- Added support for Samsung GDDR6 memory
- Wiped Tensix L1, ERISC L1, and GDDR memory during init for security

## Blackhole Changes

### Stability Improvements
- On p300 boards, fixed issue where one ASIC would drop after numerous consecutive reboots.

### New Boards
- Add support for Galaxy revC board

### Power Management
- Update limits for p300c boards:
  - Board power limit to `550 W`
  - TDP limit to `125 W`
  - GDDR temperature limit to `88 C`

### Telemetry
- New `TAG_AICLK_PPM_INFO` telemetry entry captures which arbiter is limiting AICLK and why
- Enhanced `TAG_AICLK_ARB_MIN` and `TAG_AICLK_ARB_MAX` to include arbiter IDs
- Added `targ_freq_update` trace event for debugging AICLK decisions

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v19.5.0 release can be found in [19.6 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-19.6.md).

## Full ChangeLog

The full ChangeLog from the previous v19.5.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v19.5.0...v19.6.0
