#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""
Script to open RTT console to DMC
"""

from rtt_helper import RTTHelper
from pathlib import Path

DEFAULT_CFG = (
    Path(__file__).parents[1]
    / "boards/tenstorrent/tt_blackhole/support/tt_blackhole_dmc.cfg"
)
DEFAULT_SEARCH_BASE = 0x20000000
DEFAULT_SEARCH_RANGE = 0x80000


def start_dmc_rtt():
    """
    Main function to start RTT console
    """
    rtt_helper = RTTHelper(
        cfg=DEFAULT_CFG,
        search_base=DEFAULT_SEARCH_BASE,
        search_range=DEFAULT_SEARCH_RANGE,
    )
    rtt_helper.parse_args()
    rtt_helper.run_rtt_server()


if __name__ == "__main__":
    start_dmc_rtt()
