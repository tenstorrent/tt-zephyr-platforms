#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import os
import sys

from tt_tools_common.utils_common.tools_utils import (
    detect_chips_with_callback,
)


def reset_scratch_ram(n) -> int:
    return 0x80030400 + n * 4


def reset_scratch_reg(n) -> int:
    return 0x80030060 + n * 4


POST_CODE_REG = reset_scratch_reg(0)
POST_CODE_PREFIX = 0xC0DE

UART_TT_VIRT_DISCOVERY_ADDR = reset_scratch_ram(42)
UART_TT_VIRT_MAGIC = 0x775E21A1

ARC_X = 8
ARC_Y = 0

chips = detect_chips_with_callback()
chip = chips[0]
bh = chip.as_bh()

post_code = bh.noc_read32(0, ARC_X, ARC_Y, POST_CODE_REG)
print(f"post_code: 0x{post_code:08x}")

if post_code >> 16 != POST_CODE_PREFIX:
    print(f"post code 0x{post_code:08x} does not have prefix 0x{POST_CODE_PREFIX:x}")
    sys.exit(os.EX_DATAERR)

vuart_addr = bh.noc_read32(0, ARC_X, ARC_Y, UART_TT_VIRT_DISCOVERY_ADDR)
print(f"vuart_addr: 0x{vuart_addr:08x}")

vuart_magic = bh.noc_read32(0, ARC_X, ARC_Y, vuart_addr)
if vuart_magic != UART_TT_VIRT_MAGIC:
    print(f"vuart magic 0x{vuart_magic:x} does not equal 0x{UART_TT_VIRT_MAGIC:x}")
    sys.exit(os.EX_DATAERR)


class Vuart:
    offset = {
        "magic": 0,
        "tx_buf_capacity": 4,
        "rx_buf_capacity": 8,
        "tx_head": 12,
        "tx_tail": 16,
        "tx_oflow": 20,
        "rx_head": 24,
        "rx_tail": 28,
        "buf": 32,
    }

    def __init__(self, bh, addr):
        self.bh = bh
        self.addr = vuart_addr

    def magic(self) -> int:
        return self.bh.noc_read32(0, ARC_X, ARC_Y, self.addr + self.offset["magic"])

    def tx_buf_capacity(self) -> int:
        return self.bh.noc_read32(
            0, ARC_X, ARC_Y, self.addr + self.offset["tx_buf_capacity"]
        )

    def rx_buf_capacity(self) -> int:
        return self.bh.noc_read32(
            0, ARC_X, ARC_Y, self.addr + self.offset["rx_buf_capacity"]
        )

    def tx_head(self) -> int:
        return self.bh.noc_read32(0, ARC_X, ARC_Y, self.addr + self.offset["tx_head"])

    def tx_tail(self) -> int:
        return self.bh.noc_read32(0, ARC_X, ARC_Y, self.addr + self.offset["tx_tail"])

    def tx_oflow(self) -> int:
        return self.bh.noc_read32(0, ARC_X, ARC_Y, self.addr + self.offset["tx_oflow"])

    def rx_head(self) -> int:
        return self.bh.noc_read32(0, ARC_X, ARC_Y, self.addr + self.offset["rx_head"])

    def rx_tail(self) -> int:
        return self.bh.noc_read32(0, ARC_X, ARC_Y, self.addr + self.offset["rx_tail"])

    def buf(self) -> int:
        return self.addr + 32

    def tx_buf(self) -> int:
        return self.buf()

    def rx_buf(self) -> int:
        return self.buf() + self.tx_buf_capacity()

    def __str__(self):
        return f"""
  magic:    {self.magic():x}
  tx_cap:   {self.tx_buf_capacity()}
  rx_cap:   {self.rx_buf_capacity()}
  tx_head:  {self.tx_head()}
  tx_tail:  {self.tx_tail()}
  tx_oflow: {self.tx_oflow()}
  rx_head:  {self.rx_head()}
  rx_tail:  {self.rx_tail()}
"""

    @staticmethod
    def _round_up(x, align):
        return ((x + (align - 1)) / align) * align

    def read(self) -> bytes:
        head = self.tx_head()
        tail = self.tx_tail()
        # fixme: python uses signed integer arithmetic instead of unsigned
        nbytes = tail - head

        if nbytes == 0:
            return bytes()

        buf = bytearray()
        nwords = int(nbytes / 4)
        begin = self.tx_buf()
        for i in range(begin, begin + 4 * nwords, 4):
            intval = self.bh.noc_read32(0, ARC_X, ARC_Y, i)
            for b in intval.to_bytes(4, "little"):
                buf.append(b)

        head += nwords * 4
        self.bh.noc_write32(0, ARC_X, ARC_Y, self.addr + self.offset["tx_head"], head)

        return bytes(buf)


vuart = Vuart(bh, vuart_addr)

print(f"Vuart: {vuart}")

print(vuart.read().decode("utf-8"))
