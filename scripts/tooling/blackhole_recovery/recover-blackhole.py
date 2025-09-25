#!/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0

"""
This script flashes firmware from a "recovery bundle" onto a blackhole card.
The bundle should be created using the "prepare-recovery-bundle.py" script.
"""

import argparse
from pathlib import Path
import tarfile
import tempfile
import sys
import time
import os
from intelhex import IntelHex

try:
    import pyluwen
    import yaml
    from pyocd.core.helpers import ConnectHelper
    from pyocd.flash.file_programmer import FileProgrammer
    from pyocd.flash.eraser import FlashEraser
except ImportError:
    print("Required modules not found. Please run pip install -r requirements.txt")
    sys.exit(os.EX_UNAVAILABLE)

try:
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader

sys.path.append(str(Path(__file__).parents[2]))
# Local imports
import pcie_utils
import tt_boot_fs

# Set environment variable for protobuf implementation
if os.environ.get("PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION") != "python":
    os.environ["PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION"] = "python"
from encode_spirom_bins import convert_proto_txt_to_bin_file

PYOCD_TARGET = "STM32G0B1CEUx"
ARC_PING_MSG = 0x90
DMC_PING_MSG = 0xC0

TT_Z_P_ROOT = Path(__file__).parents[3]


def parse_args():
    parser = argparse.ArgumentParser(
        description="Utility to recover blackhole card", allow_abbrev=False
    )
    parser.add_argument(
        "bundle",
        help="Filename of recovery bundle",
    )
    parser.add_argument("board", help="Board name to recover")
    parser.add_argument(
        "--board-id",
        type=str,
        default="auto",
        help="Board ID to program into the board's EEPROM",
    )
    parser.add_argument("--adapter-id", help="Adapter ID to use for pyocd")
    parser.add_argument(
        "--force",
        action="store_true",
        help="Forcibly recover the card, even if sanity checks pass",
    )
    return parser.parse_args()


def set_board_serial(hex_file, board_name, board_id):
    """
    Modifies a recovery hex file to include a new board ID in the EEPROM image
    @param hex_file: Path to hex file to patch. Should include tt-boot-fs and dmc image
    @param board_name: Name of board in protobuf definitions
    @param board_id: serial to patch for board
    """
    try:
        import read_only_pb2  # pylint: disable=import-outside-toplevel
    except ImportError as e:
        print(f"Error importing protobuf modules: {e}")
        print("Ensure the protobuf files are generated and the path is correct.")
        sys.exit(1)
    work_dir = Path(hex_file).parent
    # Dump new readonly data
    convert_proto_txt_to_bin_file(
        TT_Z_P_ROOT / "boards" / "tenstorrent" / "tt_blackhole" / "spirom_data_tables",
        board_name,
        work_dir / "generated_proto_bins",
        "read_only",
        read_only_pb2.ReadOnly,
        False,
        override={"board_id": board_id},
    )
    bootfs_data = IntelHex(hex_file).tobinstr()
    protobuf_bin = open(
        work_dir / "generated_proto_bins" / board_name / "read_only.bin", "rb"
    ).read()
    bootfs_entries = tt_boot_fs.BootFs.from_binary(bootfs_data).entries
    boardcfg = bootfs_entries["boardcfg"]
    # Recreate boardcfg entry with the protobuf data
    order = list(bootfs_entries.keys())
    order.remove("failover")
    failover = bootfs_entries["failover"]
    bootfs_entries["boardcfg"] = tt_boot_fs.FsEntry(
        False, "boardcfg", protobuf_bin, boardcfg.spi_addr, 0x0, False
    )
    bootfs_data = tt_boot_fs.BootFs(order, bootfs_entries, failover).to_intel_hex()
    with open(work_dir / "new_serial.hex", "wb") as f:
        f.write(bootfs_data)
    original = IntelHex(hex_file)
    new = IntelHex(str(work_dir / "new_serial.hex"))
    original.merge(new, overlap="replace")
    original.write_hex_file(hex_file)
    return hex_file


def check_card_status(board_config):
    """Check if the card is in a good state"""
    for pci_idx, config in enumerate(board_config):
        # See if the card is on the bus
        if not Path(f"/dev/tenstorrent/{pci_idx}").exists():
            print(f"Card {pci_idx} not found on bus")
            return False
        # Check if the card can be accessed by pyluwen
        try:
            card = pyluwen.detect_chips()[pci_idx]
            response = card.arc_msg(ARC_PING_MSG, True, True, 0, 0)
            if response[0] != 1 or response[1] != 0:
                # ping arc message failed
                print(f"ARC ping failed for ASIC {pci_idx}")
                return False
            # Test DMC ping
            response = card.arc_msg(DMC_PING_MSG, True, True, 0, 0)
            if response[0] != 1 or response[1] != 0:
                # ping dmc message failed
                print(f"DMC ping failed for ASIC {pci_idx}")
                return False
            # Check telemetry data to see if the UPI looks right
            if card.get_telemetry().board_id >> 36 != config["upi"]:
                print(f"Board ID UPI does not match expected value for ASIC {pci_idx}")
                return False
        except BaseException:
            print(f"Error accessing card with pyluwen for ASIC {pci_idx}")
            return False
        return True


def get_session(asic, adapter_id, temp_dir):
    if adapter_id is None:
        print(
            "No adapter ID provided, please select the debugger "
            "attached to STM32 if prompted"
        )
        session = ConnectHelper.session_with_chosen_probe(
            target_override=PYOCD_TARGET,
            user_script=Path(temp_dir) / asic["pyocd-config"],
        )
    else:
        session = ConnectHelper.session_with_chosen_probe(
            target_override=PYOCD_TARGET,
            user_script=Path(temp_dir) / asic["pyocd-config"],
            unique_id=adapter_id,
        )
    return session


def main():
    args = parse_args()
    with (
        tempfile.TemporaryDirectory() as temp_dir,
        tarfile.open(args.bundle, "r:gz") as tar,
    ):
        tar.extractall(path=temp_dir, filter="data")
        try:
            f = open(Path(temp_dir) / "board_metadata.yaml")
            BOARD_ID_MAP = yaml.load(f.read(), Loader=SafeLoader)
            f.close()
        except FileNotFoundError:
            print("Error: board_metadata.yaml not found in recovery bundle")
            return

        if args.board not in BOARD_ID_MAP:
            print(f"Error: board {args.board} not found in recovery bundle")
            return

        sys.path.append(str(Path(temp_dir) / args.board / "python_proto_files"))

        if (not args.force) and check_card_status(BOARD_ID_MAP[args.board]):
            print(f"All ASICs on board {args.board} are functional, skipping recovery")
            return

        for idx in range(len(BOARD_ID_MAP[args.board])):
            asic = BOARD_ID_MAP[args.board][idx]
            session = get_session(asic, args.adapter_id, temp_dir)
            session.open()
            # First, reset the DMC and see if we can reach the card
            session.board.target.reset_and_halt()
            session.board.target.resume()
            session.close()
            time.sleep(2)
            pcie_utils.rescan_pcie()
            time.sleep(2)

        if (not args.force) and check_card_status(BOARD_ID_MAP[args.board]):
            print(f"All ASICs on board {args.board} are functional, skipping recovery")
            return

        # First, erase the flash on all ASICs
        for idx in range(len(BOARD_ID_MAP[args.board])):
            asic = BOARD_ID_MAP[args.board][idx]
            session = get_session(asic, args.adapter_id, temp_dir)
            session.open()
            # Erase the flash
            print(f"Erasing flash on ASIC {idx}...")
            FlashEraser(session, FlashEraser.Mode.CHIP).erase()
            session.close()
            time.sleep(1)

        # Now, program the new binaries to all ASICs
        for idx in range(len(BOARD_ID_MAP[args.board])):
            asic = BOARD_ID_MAP[args.board][idx]
            recovery_hex = (
                Path(temp_dir) / args.board / f"{asic['bootfs-name']}_recovery.hex"
            )
            if not recovery_hex.exists():
                print(f"Error: recovery hex file {recovery_hex} not found in bundle")
                return
            if args.board_id == "auto":
                board_id = asic["upi"] << 36 | (0x1 << 32)
            else:
                board_id = int(args.board_id, 0)
            recovery_hex = set_board_serial(
                str(recovery_hex), asic["protobuf-name"], board_id
            )
            session = get_session(asic, args.adapter_id, temp_dir)
            session.open()
            # Program the recovery hex
            print(f"Flashing {recovery_hex} to ASIC {idx}...")
            FileProgrammer(session).program(str(recovery_hex), file_format="hex")
            session.board.target.reset_and_halt()
            session.board.target.resume()
            session.close()
            # Delay a moment for ASIC boot
            time.sleep(2)
            pcie_utils.rescan_pcie()
            time.sleep(2)
        # Now, check if all asics are functional
        if not check_card_status(BOARD_ID_MAP[args.board]):
            raise RuntimeError("Card did not recover successfully, try a reboot?")

        print("Card recovered successfully")


if __name__ == "__main__":
    main()
