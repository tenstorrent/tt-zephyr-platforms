# Blackhole PCIe Card Automated Recovery Script

This script helps simplify the recovery of blackhole PCIe cards, by
reprogramming their flash eeprom via the device management controller (DMC).

In order to use this script, you should have an SWD connection to the DMC,
usually made via a debug board and an ST-Link probe.

Usage:
```
./recover-blackhole.py p100a --adapter-id  B55B5A1A000000005833EF01
Phase 1: Rescanning PCIe bus
Powering off device at /sys/bus/pci/devices/0000:01:00.0
Phase 2: Programming DMC firmware
[==================================================] 100%
Phase 3: Rewriting EEPROM with SMC and DMC firmware
[==================================================] 100%
Phase 4: Resetting DMC and waiting for firmware update
Waiting 60 seconds for the DMC in case it triggers a firmware update
Phase 5: Writing EEPROM with default UPI configuration
[==================================================] 100%
Card appears functional, but your UPI was rewritten. Please contact support for assistance.
```

The script will exit once the card is recovered, and only attempt each
(more invasive) phase if the prior recovery attempt fails.

The script currently should support `p100`, `p100a`, `p150a`, `p150b`, and `p150c` cards.
