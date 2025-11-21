#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""
Update BAR4 size in firmware bundle.

This script modifies the pcie_bar4_size field in the pci0_property_table and
pci1_property_table structures within the cmfwcfg (firmware configuration table)
stored in a firmware bundle.

Usage:
    # List available boards in a firmware bundle
    ./update-bar4-size.py --input fw_pack.fwbundle --board ?

    # Update BAR4 size to 32768 MiB for a specific board
    ./update-bar4-size.py --input fw_pack.fwbundle --output fw_pack_out.fwbundle --board P100A-1 --size 32768

    # Update BAR4 size for all boards in the bundle
    ./update-bar4-size.py --input fw_pack.fwbundle --output fw_pack_out.fwbundle --size 32768

    # Disable BAR4 (set to 0)
    ./update-bar4-size.py --input fw_pack.fwbundle --output fw_pack_out.fwbundle --board P100A-1 --size 0
"""

import os
import logging
import subprocess
import sys
import argparse
import tempfile
import tarfile

from pathlib import Path

BOOTFS_FWTABLE_NAME = "cmfwcfg"
TT_Z_P_ROOT = Path(__file__).parents[1]

sys.path.append(str(TT_Z_P_ROOT / "scripts"))

import tt_boot_fs  # noqa: E402
import tt_fwbundle  # noqa: E402

_logger = logging.getLogger(__name__)


# NOTE: A small framing layer was needed to bridge Google protobuf and nanopb functionality
# due to the PB_DECODE_NULLTERMINATED framing method.
#
# Nanopb PB_ENCODE_NULLTERMINATED adds a single 0 byte to the end of an endoded message.
# PB_DECODE_NULLTERMINATED reads that as a 0 tag and ends. google protobuf doesn’t support this.
# To allow google protobuf use we: add the 0 and then pad to a multiple of 4 with 0 followed by
# the number of padding bytes added. Before google PB decode, look at the last byte.
#
# 0: remove 1 byte (0)
# 1: remove 2 bytes (0, 1)
# 2: remove 3 bytes (0, X, 2)
# 3: remove 4 bytes (0, X, X, 3)
def nanopb_remove_framing(b: bytes) -> bytes:
    last_byte = b[-1]
    bytes_to_remove = last_byte + 1
    return b[:-bytes_to_remove]


def nanopb_add_framing(b: bytes) -> bytes:
    # duplicate b, so that we leave the original 'b' object unmodified
    c = b[:]
    bytes_to_add = 4 - (len(c) % 4)
    c += bytes(range(bytes_to_add))
    return c


def parse_args():
    parser = argparse.ArgumentParser(description="Update BAR4 size", allow_abbrev=False)
    parser.add_argument(
        "--board",
        type=str,
        help=(
            "Name of the board to update BAR4 size for. Can be specified multiple times. "
            "If not specified, all boards will be updated. "
            "If '?' is specified then available board names are listed."
        ),
        action="append",
    )
    parser.add_argument(
        "--bus",
        type=int,
        help=(
            "Number of the bus to update BAR4 size for. Can be specified multiple times. "
            "If not specified, all busses will be updated. "
        ),
        action="append",
    )
    parser.add_argument(
        "--input",
        type=Path,
        required=True,
        help="Path to the input firmware bundle",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Path to the output firmware bundle",
    )
    parser.add_argument(
        "--size",
        type=int,
        default=0,
        help="The new power-of-two size for BAR4 in MiB (0 to disable). Requires a cold reboot to take effect",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable verbose logging",
    )
    args = parser.parse_args()

    return args


def get_board_names_with_cmfwcfg_from_bundle_metadata(bundle_metadata) -> set[str]:
    board_names = []
    for meta_name, meta_data in bundle_metadata.items():
        if meta_name == "manifest":
            # not a board
            continue

        board_name = meta_name
        for part in meta_data["bootfs"]:
            if part["image_tag"] == BOOTFS_FWTABLE_NAME:
                board_names.append(board_name)
                break

    return set(board_names)


def get_board_metadata_from_bundle_metadata(
    bundle_metadata, board_names
) -> dict[str, dict]:
    board_meta = {}

    for meta_name, meta_data in bundle_metadata.items():
        if meta_name == "manifest":
            # not a board
            continue

        board_name = meta_name
        if board_name not in board_names:
            continue

        board_meta[board_name] = meta_data

    return board_meta


def print_board_names(board_names: set[str]):
    # List available board names and exit
    if not board_names:
        _logger.warning(f"No boards found with bootfs entry {BOOTFS_FWTABLE_NAME}")

    for board in board_names:
        _logger.info(board)


def filter_boards(arg_boards: list[str], board_names: set[str]) -> set[str]:
    # Filter supplied board names to only those that apply
    for b in arg_boards:
        if b not in board_names:
            _logger.warning(
                f"board '{b}' does not have a bootfs entry {BOOTFS_FWTABLE_NAME} and will be skipped"
            )
            continue

    return {b for b in arg_boards if b in board_names}


def get_cmfwcfg_from_bootfs_metadata(bootfs):
    for part in bootfs:
        if part["image_tag"] == BOOTFS_FWTABLE_NAME:
            return part

    return None


def iterate_bar4_sizes(
    fw_table_pb2_pkg,
    bootfs_bin_path: Path,
    size: int,
    busses: list[int],
    verify: bool = False,
    verbose: bool = False,
):
    bootfs_b16_path = bootfs_bin_path.with_suffix(".b16")
    os.rename(bootfs_bin_path, bootfs_b16_path)
    bootfs_bytes = tt_boot_fs.extract_all(bootfs_b16_path, input_base64=True)

    bootfs = tt_boot_fs.BootFs.from_binary(bootfs_bytes)
    cmfwcfg_entry = bootfs.entries[BOOTFS_FWTABLE_NAME]

    cmfwcfg_addr = cmfwcfg_entry.spi_addr
    cmfwcfg_size = len(cmfwcfg_entry.data)
    cmfwcfg_bytes = cmfwcfg_entry.data
    _logger.debug(
        f"{BOOTFS_FWTABLE_NAME} at SPI address: 0x{cmfwcfg_addr:x}, size: {cmfwcfg_size} bytes"
    )

    # Remove nanopb framing
    if verbose:
        tt_boot_fs.hexdump(cmfwcfg_addr, cmfwcfg_bytes, checksum=True)
    proto_bytes = nanopb_remove_framing(cmfwcfg_bytes)

    # Parse the protobuf message
    fw_table = fw_table_pb2_pkg.FwTable()
    fw_table.ParseFromString(proto_bytes)

    def safe_has_field(table, field_name: str) -> bool:
        try:
            return table.HasField(field_name)
        except ValueError:
            return False

    # filter busses. The assumption is that bus N+1 only exists if bus N exists
    bus = 0
    filtered_busses = []
    while safe_has_field(fw_table, f"pci{bus}_property_table"):
        if not busses or bus in busses:
            filtered_busses.append(bus)
        bus += 1
    busses = filtered_busses

    if verify:
        # Just verify that the sizes are as expected
        for bus in busses:
            pci_table = getattr(fw_table, f"pci{bus}_property_table")
            if pci_table.pcie_bar4_size != size:
                raise ValueError(
                    f"Verification failed: pci{bus}_property_table.pcie_bar4_size is {pci_table.pcie_bar4_size} MiB, expected {size} MiB"
                )
            _logger.info(
                f"Verified pci{bus}_property_table.pcie_bar4_size is {pci_table.pcie_bar4_size} MiB"
            )
        return bootfs_bytes

    # Update BAR4 size
    for bus in busses:
        pci_table = getattr(fw_table, f"pci{bus}_property_table")
        _logger.debug(
            f"Current pci{bus}_property_table.pcie_bar4_size: {pci_table.pcie_bar4_size} MiB"
        )
        pci_table.pcie_bar4_size = size
        _logger.debug(f"Updated pci{bus}_property_table.pcie_bar4_size to {size} MiB")

    # Serialize back to bytes
    proto_bytes = fw_table.SerializeToString()

    # Add nanopb framing
    # The calls below can be uncommented to see how deframing works
    # tt_boot_fs.hexdump(cmfwcfg_addr, proto_bytes, checksum=True)
    cmfwcfg_bytes = nanopb_add_framing(proto_bytes)
    if verbose:
        tt_boot_fs.hexdump(cmfwcfg_addr, cmfwcfg_bytes, checksum=True)

    # Update the bootfs with the new cmfwcfg
    bootfs.entries[BOOTFS_FWTABLE_NAME].data = cmfwcfg_bytes

    bootfs_bytes = bootfs.to_binary(True)

    with open(bootfs_bin_path, "wb") as f:
        # Convert back to the b16 encoded format
        f.write(bytes(bootfs.to_b16(), "ascii"))

    os.remove(bootfs_b16_path)


def do_update(
    input: Path,
    output: Path,
    board: list[str],
    bus: list[int],
    size: int,
    verbose: bool = False,
) -> int:
    if size < 0:
        _logger.error("Size may not be negative")
        return os.EX_DATAERR

    if size > 0 and size & (size - 1) != 0:
        _logger.error("Non-zero size must be a power of two")
        return os.EX_DATAERR

    if verbose:
        logging.basicConfig(format="%(message)s", level=logging.DEBUG)

    # Generate necessary protobuf python bindings
    with tempfile.TemporaryDirectory() as proto_tempdir:
        _logger.debug("Generating protobuf python bindings for fw_table_pb2..")

        os.environ["PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION"] = "python"

        OUTPUT_DIR = Path(proto_tempdir)
        OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
        SPIROM_PROTOBUFS = (
            TT_Z_P_ROOT / "lib" / "tenstorrent" / "bh_arc" / "spirom_protobufs"
        )

        subprocess.run(
            [
                TT_Z_P_ROOT
                / ".."
                / "modules"
                / "lib"
                / "nanopb"
                / "generator"
                / "protoc",
                f"--python_out={OUTPUT_DIR}",
                f"{SPIROM_PROTOBUFS}/fw_table.proto",
                "-I",
                f"{SPIROM_PROTOBUFS}",
            ],
            capture_output=True,
            check=True,
        )
        sys.path.append(str(OUTPUT_DIR))
        import fw_table_pb2  # noqa: E402

        try:
            _logger.info(f"Loading fwbundle {input}")
            bundle_metadata = tt_fwbundle.bundle_metadata(input)
        except Exception as e:
            _logger.error(f"Error loading fwbundle metadata: {e}")
            return os.EX_DATAERR

        board_names = get_board_names_with_cmfwcfg_from_bundle_metadata(bundle_metadata)
        if not board:
            # No board names have been supplied, so use all of them that apply
            board = board_names

        if "?" in board:
            print_board_names(board_names)
            return os.EX_OK

        # Ensure only valid board names are used
        board = filter_boards(board, board_names)

        if not board:
            _logger.info("No valid boards to process")
            return os.EX_DATAERR

        if not bus:
            # No bus numbers have been supplied, so use all of them that apply
            bus = []
        # sort bus numbers so that it's deterministic to cycle through them
        bus = sorted(bus)

        with tarfile.open(input, "r:gz") as tar_in:
            with tempfile.TemporaryDirectory() as tempdir:
                tar_in.extractall(path=tempdir)

                # Iterate over each board to extract the cmfwcfg entry, modify it, and write it back
                for b in board:
                    _logger.debug(f"Processing board '{b}'")
                    iterate_bar4_sizes(
                        fw_table_pb2,
                        Path(tempdir) / b / "image.bin",
                        size,
                        bus,
                        verbose=verbose,
                    )

                # Create a new fwbundle with the modified files
                verb = "Creating"
                if output.exists():
                    verb = "Overwriting existing"
                    os.remove(output)

                _logger.info(f"{verb} output fwbundle: {output}")
                with tarfile.open(output, "x:gz") as tar_out:
                    tar_out.add(tempdir, arcname=".")

        # verify that the firmware table includes the expected size
        with tarfile.open(output, "r:gz") as tar_in:
            with tempfile.TemporaryDirectory() as tempdir:
                tar_in.extractall(path=tempdir)

                # Iterate over each board to extract the cmfwcfg entry, modify it, and write it back
                for b in board:
                    _logger.debug(f"Verifying board '{b}'")
                    iterate_bar4_sizes(
                        fw_table_pb2,
                        Path(tempdir) / b / "image.bin",
                        size,
                        bus,
                        verbose=verbose,
                        verify=True,
                    )

    return os.EX_OK


def main():
    args = parse_args()

    return do_update(
        args.input, args.output, args.board, args.bus, args.size, args.verbose
    )


if __name__ == "__main__":
    sys.exit(main())
