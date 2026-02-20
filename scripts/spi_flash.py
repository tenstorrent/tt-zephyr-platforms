#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0

"""
Standalone SPI flash utility for Blackhole PCIe cards.

Erases or programs the external SPI EEPROM through the STM32 DMC's
JTAG/SWD debug interface using pyocd. The script automatically handles
all ASICs on a board (e.g. two ASICs for p300 variants).

Examples:
    python spi_flash.py --board-name p100a full_erase
    python spi_flash.py --board-name p100a write_from_ihex preflash.ihex
    python spi_flash.py --board-name p300a full_erase          # erases both ASICs
    python spi_flash.py --board-name p300a write_from_ihex preflash.ihex
"""

import argparse
import logging
import os
import sys
import time
from pathlib import Path

from pyocd.flash.file_programmer import FileProgrammer
from pyocd.flash.eraser import FlashEraser

import pyocd_utils

logger = logging.getLogger(Path(__file__).stem)


# ---------------------------------------------------------------------------
# Sub-commands
# ---------------------------------------------------------------------------


def cmd_full_erase(args):
    """Erase the SPI flash on every ASIC of the specified board."""
    board_metadata = pyocd_utils.load_board_metadata()
    board_name = args.board_name

    if board_name not in board_metadata:
        logger.error(
            "Unknown board name: %s. Supported: %s",
            board_name,
            list(board_metadata.keys()),
        )
        return os.EX_DATAERR

    asics = board_metadata[board_name]
    for idx, asic in enumerate(asics):
        pyocd_config = pyocd_utils.PYOCD_FLM_PATH / asic["pyocd-config"]
        logger.info("Erasing SPI flash on ASIC %d (%s)...", idx, asic["name"])

        session = pyocd_utils.get_session(pyocd_config, args.adapter_id, args.no_prompt)
        session.open()
        try:
            FlashEraser(session, FlashEraser.Mode.CHIP).erase()
        finally:
            session.close()

        logger.info("ASIC %d SPI flash erased.", idx)
        time.sleep(1)

    logger.info(
        "Full erase complete for board %s (%d ASIC(s)).", board_name, len(asics)
    )
    return os.EX_OK


def cmd_write_from_ihex(args):
    """Write an Intel HEX file to SPI flash on every ASIC of the specified board."""
    board_metadata = pyocd_utils.load_board_metadata()
    board_name = args.board_name

    if board_name not in board_metadata:
        logger.error(
            "Unknown board name: %s. Supported: %s",
            board_name,
            list(board_metadata.keys()),
        )
        return os.EX_DATAERR

    ihex_file = Path(args.file)
    if not ihex_file.is_file():
        logger.error("Intel HEX file not found: %s", ihex_file)
        return os.EX_NOINPUT

    asics = board_metadata[board_name]
    for idx, asic in enumerate(asics):
        pyocd_config = pyocd_utils.PYOCD_FLM_PATH / asic["pyocd-config"]
        logger.info(
            "Writing %s to SPI flash on ASIC %d (%s)...",
            ihex_file,
            idx,
            asic["name"],
        )

        session = pyocd_utils.get_session(pyocd_config, args.adapter_id, args.no_prompt)
        session.open()
        try:
            FileProgrammer(session).program(str(ihex_file), file_format="hex")
        finally:
            session.close()

        logger.info("ASIC %d SPI flash programmed.", idx)
        time.sleep(1)

    logger.info("Write complete for board %s (%d ASIC(s)).", board_name, len(asics))
    return os.EX_OK


# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------


def parse_args():
    parser = argparse.ArgumentParser(
        description="SPI flash utility for Blackhole PCIe cards",
        allow_abbrev=False,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--board-name",
        type=str,
        required=True,
        help="Board name as listed in board_metadata.yaml (e.g. p100a, p150a, p300a)",
    )
    parser.add_argument(
        "--adapter-id",
        type=str,
        default=None,
        help="ST-Link adapter serial / unique ID",
    )
    parser.add_argument(
        "--no-prompt",
        action="store_true",
        default=False,
        help="Do not prompt for adapter selection; use the first available probe",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="count",
        default=0,
        help="Increase logging verbosity (repeat for more)",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    # full_erase
    subparsers.add_parser(
        "full_erase",
        help="Erase the entire SPI flash on all ASICs of the board",
    )

    # write_from_ihex
    write_parser = subparsers.add_parser(
        "write_from_ihex",
        help="Write an Intel HEX file to SPI flash on all ASICs of the board",
    )
    write_parser.add_argument(
        "file",
        type=str,
        help="Path to the Intel HEX (.ihex) file to program",
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

    if args.command == "full_erase":
        return cmd_full_erase(args)
    elif args.command == "write_from_ihex":
        return cmd_write_from_ihex(args)
    else:
        logger.error("Unknown command: %s", args.command)
        return os.EX_USAGE


if __name__ == "__main__":
    sys.exit(main())
