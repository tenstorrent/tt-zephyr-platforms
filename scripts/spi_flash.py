#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0

"""
Standalone SPI flash utility for Blackhole PCIe cards.

Erases or programs the external SPI EEPROM through the STM32 DMC's
JTAG/SWD debug interface using pyocd. By default the script operates on
all ASICs of a board. Use ``--asic-index`` to target a single ASIC
(e.g. on p300 boards which have two).

Examples:
    python spi_flash.py --board-name p100a full_erase
    python spi_flash.py --board-name p100a write_from_ihex preflash.ihex
    python spi_flash.py --board-name p300a full_erase              # erases both ASICs
    python spi_flash.py --board-name p300a --asic-index 0 full_erase   # left ASIC only
    python spi_flash.py --board-name p300a write_from_ihex preflash.ihex
"""

import argparse
import base64
import logging
import os
import sys
import tarfile
import tempfile
import time
from pathlib import Path

from pyocd.flash.file_programmer import FileProgrammer
from pyocd.flash.eraser import FlashEraser

import pyocd_utils
import tt_boot_fs

logger = logging.getLogger(Path(__file__).stem)


# ---------------------------------------------------------------------------
# Sub-commands
# ---------------------------------------------------------------------------


def _select_asics(board_metadata, board_name, asic_index):
    """Return the list of (index, asic) pairs to operate on.

    If *asic_index* is ``None`` all ASICs for the board are returned.
    Otherwise only the requested ASIC is returned (with validation).
    """
    if board_name not in board_metadata:
        logger.error(
            "Unknown board name: %s. Supported: %s",
            board_name,
            list(board_metadata.keys()),
        )
        return None

    asics = board_metadata[board_name]

    if asic_index is not None:
        if asic_index < 0 or asic_index >= len(asics):
            logger.error(
                "ASIC index %d out of range for board %s (has %d ASIC(s))",
                asic_index,
                board_name,
                len(asics),
            )
            return None
        return [(asic_index, asics[asic_index])]

    return list(enumerate(asics))


def cmd_full_erase(args):
    """Erase the SPI flash on the specified board (all ASICs or one)."""
    board_metadata = pyocd_utils.load_board_metadata()
    selected = _select_asics(board_metadata, args.board_name, args.asic_index)
    if selected is None:
        return os.EX_DATAERR

    for idx, asic in selected:
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
        "Full erase complete for board %s (%d ASIC(s)).",
        args.board_name,
        len(selected),
    )
    return os.EX_OK


def cmd_write_from_ihex(args):
    """Write an Intel HEX file to SPI flash on the specified board (all ASICs or one)."""
    board_metadata = pyocd_utils.load_board_metadata()
    selected = _select_asics(board_metadata, args.board_name, args.asic_index)
    if selected is None:
        return os.EX_DATAERR

    ihex_file = Path(args.file)
    if not ihex_file.is_file():
        logger.error("Intel HEX file not found: %s", ihex_file)
        return os.EX_NOINPUT

    for idx, asic in selected:
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

    logger.info(
        "Write complete for board %s (%d ASIC(s)).",
        args.board_name,
        len(selected),
    )
    return os.EX_OK


def _decode_b16_image(image_path):
    """Decode a base16-encoded image file to raw bytes."""
    data = bytearray()
    with open(image_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("@"):
                offset = int(line[1:], 10)
                if offset > len(data):
                    data.extend(b"\xff" * (offset - len(data)))
            else:
                data.extend(base64.b16decode(line))
    return bytes(data)


def cmd_write_from_fwbundle(args):
    """Extract a firmware bundle and write it to SPI flash via pyocd.

    The fwbundle is extracted, each ASIC's image.bin is decoded from base16
    to binary, validated as a bootfs, converted to Intel HEX, and then
    programmed through pyocd's ``FileProgrammer``.
    """
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

    board_metadata = pyocd_utils.load_board_metadata()
    selected = _select_asics(board_metadata, args.board_name, args.asic_index)
    if selected is None:
        return os.EX_DATAERR

    fwbundle = Path(args.file)
    if not fwbundle.is_file():
        logger.error("Firmware bundle not found: %s", fwbundle)
        return os.EX_NOINPUT

    with tempfile.TemporaryDirectory() as temp_dir:
        try:
            with tarfile.open(fwbundle, "r") as tar:
                tar.extractall(path=temp_dir)
        except tarfile.TarError as e:
            logger.error("Failed to extract firmware bundle: %s", e)
            return os.EX_DATAERR

        for idx, asic in selected:
            asic_name = asic["name"]
            image_path = Path(temp_dir) / asic_name / "image.bin"
            if not image_path.is_file():
                logger.error(
                    "image.bin not found for %s in firmware bundle",
                    asic_name,
                )
                return os.EX_DATAERR

            logger.info("Decoding firmware image for %s...", asic_name)
            raw = _decode_b16_image(image_path)

            bootfs = tt_boot_fs.BootFs.from_binary(raw)
            ihex_data = bootfs.to_intel_hex(True)

            pyocd_config = pyocd_utils.PYOCD_FLM_PATH / asic["pyocd-config"]
            logger.info(
                "Writing firmware bundle to SPI flash on ASIC %d (%s)...",
                idx,
                asic_name,
            )

            ihex_file = Path(temp_dir) / f"asic_{idx}.hex"
            with open(ihex_file, "wb") as f:
                f.write(ihex_data)

            session = pyocd_utils.get_session(
                pyocd_config, args.adapter_id, args.no_prompt
            )
            session.open()
            try:
                FileProgrammer(session).program(str(ihex_file), file_format="hex")
            finally:
                session.close()

            logger.info("ASIC %d SPI flash programmed from firmware bundle.", idx)
            time.sleep(1)

    logger.info(
        "Firmware bundle write complete for board %s (%d ASIC(s)).",
        args.board_name,
        len(selected),
    )
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
        "--asic-index",
        type=int,
        default=None,
        help="Operate on a single ASIC by index (0-based). "
        "If omitted, all ASICs on the board are targeted.",
    )
    pyocd_utils.add_common_args(parser)

    subparsers = parser.add_subparsers(dest="command", required=True)

    # full_erase
    subparsers.add_parser(
        "full_erase",
        help="Erase the entire SPI flash (all ASICs unless --asic-index is given)",
    )

    # write_from_ihex
    write_parser = subparsers.add_parser(
        "write_from_ihex",
        help="Write an Intel HEX file to SPI flash (all ASICs unless --asic-index is given)",
    )
    write_parser.add_argument(
        "file",
        type=str,
        help="Path to the Intel HEX (.ihex) file to program",
    )

    # write_from_fwbundle
    fwbundle_parser = subparsers.add_parser(
        "write_from_fwbundle",
        help="Write a firmware bundle (.fwbundle) to SPI flash "
        "(all ASICs unless --asic-index is given)",
    )
    fwbundle_parser.add_argument(
        "file",
        type=str,
        help="Path to the firmware bundle (.fwbundle) file",
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
    elif args.command == "write_from_fwbundle":
        return cmd_write_from_fwbundle(args)
    else:
        logger.error("Unknown command: %s", args.command)
        return os.EX_USAGE


if __name__ == "__main__":
    sys.exit(main())
