#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""
Query Tensix disable count from running firmware
"""

import pyluwen
import os
import sys

import logging

_logger = logging.getLogger(__name__)


def main():
    try:
        chips = pyluwen.detect_chips()
    except Exception as e:
        _logger.error(f"Error detecting chips: {e}")
        return os.EX_UNAVAILABLE

    for idx, chip in enumerate(chips):
        telemetry = chip.get_telemetry()
        tensix_enable_mask = telemetry.tensix_enabled_col
        tensix_disable_count = 14 - bin(tensix_enable_mask).count("1")
        print(f"Chip {idx}: Tensix column disable count = {tensix_disable_count}")
    return os.EX_OK


if __name__ == "__main__":
    exit_code = main()
    sys.exit(exit_code)
