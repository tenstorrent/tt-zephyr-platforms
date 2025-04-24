#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""
Script to open RTT console to BMC
"""

from rtt_helper import RTTHelper
from pathlib import Path

DEFAULT_CFG = (
    Path(__file__).parents[1]
    / "boards/tenstorrent/tt_blackhole/support/tt_blackhole_bmc.cfg"
)
DEFAULT_SEARCH_BASE = 0x20000000
DEFAULT_SEARCH_RANGE = 0x80000


def start_bmc_rtt():
    """
    Main function to start RTT console
    """
    rtt_helper = RTTHelper(
        default_cfg=DEFAULT_CFG,
        default_search_base=DEFAULT_SEARCH_BASE,
        default_search_range=DEFAULT_SEARCH_RANGE,
    )
    rtt_helper.parse_args()
    rtt_helper.run_rtt_server()


if __name__ == "__main__":
    start_bmc_rtt()
