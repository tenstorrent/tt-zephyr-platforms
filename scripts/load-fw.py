#!/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0
"""
Script to load firmware onto a Tenstorrent chip using pyluwen.
Usage: python3 load-fw.py <firmware_file> <jump_address> <load_address>
Example for production firmware:
python3 ./scripts/load-fw.py build/smc/zephyr/zephyr.bin 0x10010400 0x10010000
"""

import sys
import pyluwen

ARC_SCRATCH0 = 0x80030400
SCRATCH_MAGIC = 0xCAFEBABE

chip = pyluwen.detect_chips()[0]

jump_addr = int(sys.argv[2], 0)
load_addr = int(sys.argv[3], 0)

# Use an ARC message to load the firmware
chip.arc_msg(0xC5, True, False, jump_addr & 0xFFFF, jump_addr >> 16, 1000)
scratch0 = chip.axi_read32(ARC_SCRATCH0)  # Read the ready signal
while scratch0 != SCRATCH_MAGIC:
    scratch0 = chip.axi_read32(ARC_SCRATCH0)  # Wait for ARC to be halted
print("ARCs halted, loading firmware...")

with open(sys.argv[1], "rb") as f:
    fw_data = f.read()
    # Write the firmware to the chip's memory
    chip.axi_write(load_addr, fw_data)

# Now, we need to release the ARC to run the new firmware
scratch0 = chip.axi_read32(ARC_SCRATCH0)
scratch0 = 0  # Clear the halt signal
chip.axi_write32(ARC_SCRATCH0, scratch0)
print("Firmware load complete.")
