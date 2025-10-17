"""
Script to load firmware onto a Tenstorrent chip using pyluwen.
Notes:
- the firmware needs to be in binary format, and built to load from
  the address provided as the second argument.
- Firmware load has been tested using an address of 0x10040000.
- To build an SMC app with this offset, build with
  -DCONFIG_SRAM_BASE_ADDRESS=0x10040000
- the message return from luwen might time out. This is expected, as the
  chip is resetting into the new firmware.
"""

import sys
import pyluwen

chip = pyluwen.detect_chips()[0]

addr = int(sys.argv[2], 0)

with open(sys.argv[1], "rb") as f:
    fw_data = f.read()
    # Write the firmware to the chip's memory
    chip.axi_write(addr, fw_data)

# Use an ARC message to load the firmware
chip.arc_msg(0xC2, True, False, addr & 0xFFFF, addr >> 16, 1000)
