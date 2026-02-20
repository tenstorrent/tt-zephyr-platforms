#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""
Helper script to verify PCIe enumeration of Tenstorrent cards.

Rescans the PCIe bus, detects chips via pyluwen, and verifies that ARC
and DMC are responsive via ping messages. Intended for use in CI workflows
to validate that a card is alive after flashing.
"""

import argparse
import logging
import sys

from pathlib import Path

sys.path.append(str(Path(__file__).parents[1]))
import pcie_utils


def parse_args():
    parser = argparse.ArgumentParser(
        description="Check PCIe enumeration of a Tenstorrent card",
        allow_abbrev=False,
    )
    parser.add_argument(
        "--asic-id",
        type=int,
        default=0,
        help="ASIC index to check (default: 0)",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=60,
        help="Timeout in seconds to wait for enumeration (default: 60)",
    )
    parser.add_argument(
        "--no-wait",
        action="store_true",
        help="Check once without retrying",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Enable verbose logging",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(level=level, format="%(levelname)s: %(message)s")

    if args.no_wait:
        pcie_utils.rescan_pcie()
        ok = pcie_utils.check_card_status(args.asic_id)
    else:
        ok = pcie_utils.wait_for_enum(args.asic_id, args.timeout)

    if ok:
        print(f"PASS: Card {args.asic_id} enumerated and responsive")
        return 0
    else:
        print(f"FAIL: Card {args.asic_id} not enumerated or not responsive")
        return 1


if __name__ == "__main__":
    sys.exit(main())
