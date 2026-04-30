#!/usr/bin/env python3

# Copyright (c) 2026 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0

"""
STM32 DMC internal flash programmer for Blackhole PCIe cards.

Programs firmware files (HEX, ELF) to the STM32 DMC's internal flash
via the SWD debug interface using pyocd.

Examples:
    python dmc_flash.py --no-prompt flash app.signed.hex mcuboot.elf
    python dmc_flash.py --no-prompt --adapter-id 1234 flash app.hex
"""

import argparse
import logging
import os
import sys
from pathlib import Path

from pyocd.flash.file_programmer import FileProgrammer

import pyocd_utils

logger = logging.getLogger(Path(__file__).stem)


def cmd_flash(args):
    """Program one or more firmware files to STM32 internal flash, then reset."""
    for fw_file in args.files:
        fw_path = Path(fw_file)
        if not fw_path.is_file():
            logger.error("Firmware file not found: %s", fw_path)
            return os.EX_NOINPUT

    session = pyocd_utils.get_session(
        pyocd_config=None, adapter_id=args.adapter_id, no_prompt=args.no_prompt
    )
    session.open()
    try:
        for fw_file in args.files:
            logger.info("Programming %s ...", fw_file)
            FileProgrammer(session).program(fw_file)
        logger.info("Resetting target...")
        session.board.target.reset_and_halt()
        session.board.target.resume()
    finally:
        session.close()

    logger.info("Flash complete.")
    return os.EX_OK


def parse_args():
    parser = argparse.ArgumentParser(
        description="STM32 DMC internal flash programmer",
        allow_abbrev=False,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    pyocd_utils.add_common_args(parser)

    subparsers = parser.add_subparsers(dest="command", required=True)

    flash_parser = subparsers.add_parser(
        "flash",
        help="Program firmware file(s) to STM32 internal flash and reset",
    )
    flash_parser.add_argument(
        "files",
        nargs="+",
        help="Firmware file(s) to program (.hex, .elf)",
    )

    return parser.parse_args()


def main():
    args = parse_args()

    level = logging.WARNING
    if args.verbose >= 2:
        level = logging.DEBUG
    elif args.verbose >= 1:
        level = logging.INFO
    logging.basicConfig(level=level, format="%(name)s: %(levelname)s: %(message)s")

    if args.command == "flash":
        return cmd_flash(args)
    else:
        logger.error("Unknown command: %s", args.command)
        return os.EX_USAGE


if __name__ == "__main__":
    sys.exit(main())
