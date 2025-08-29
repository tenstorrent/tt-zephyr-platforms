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

try:
    import pyluwen
    import yaml
    from pyocd.core.helpers import ConnectHelper
    from pyocd.flash.file_programmer import FileProgrammer
except ImportError:
    print("Required modules not found. Please run pip install -r requirements.txt")
    sys.exit(os.EX_UNAVAILABLE)

try:
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader

PYOCD_TARGET = "STM32G0B1CEUx"

# Local imports
import pcie_utils
import tt_boot_fs

def parse_args():
    parser = argparse.ArgumentParser(
        description="Utility to recover blackhole card", allow_abbrev=False
    )
    parser.add_argument(
        "bundle",
        help="Filename of recovery bundle",
    )
    parser.add_argument(
        "board",
        help="Board name to recover"
    )
    parser.add_argument(
        "--serial-number",
        type=str,
        help="Serial number to program into the board's EEPROM",
    )
    parser.add_argument(
        "--adapter-id",
        help="Adapter ID to use for pyocd"
    )
    return parser.parse_args()

def check_card_status(pci_idx, config):
    """Check if the card is in a good state"""
    # See if the card is on the bus
    if not Path(f"/dev/tenstorrent/{pci_idx}").exists():
        return False
    # Check if the card can be accessed by pyluwen
    try:
        card = pyluwen.detect_chips()[pci_idx]
        response = card.arc_msg(ARC_PING_MSG, True, True, 0, 0)
        if response[0] != 1 or response[1] != 0:
            # ping arc message failed
            return False
        # Test DMC ping
        response = card.arc_msg(DMC_PING_MSG, True, True, 0, 0)
        if response[0] != 1 or response[1] != 0:
            # ping dmc message failed
            return False
        # Check telemetry data to see if the UPI looks right
        if card.get_telemetry().board_id >> 36 != config["upi"]:
            return False
    except BaseException:
        return False
    return True

def main():
    args = parse_args()
    with tempfile.TemporaryDirectory() as temp_dir, tarfile.open(args.bundle, "r:gz") as tar:
        tar.extractall(path=temp_dir, filter='data')
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

        # Now excute flash recovery for the board
        for idx in range(len(BOARD_ID_MAP[args.board])):
            asic = BOARD_ID_MAP[args.board][idx]
            if check_card_status(idx, asic):
                print(f"ASIC {idx} appears functional, skipping")
                continue
            recovery_hex = Path(temp_dir) / args.board / f"{asic['bootfs-name']}_recovery.hex"
            if not recovery_hex.exists():
                print(f"Error: recovery hex file {recovery_hex} not found in bundle")
                return
            print(f"Flashing {recovery_hex} to ASIC {idx}...")
            if args.adapter_id is None:
                print(
                    "No adapter ID provided, please select the debugger "
                    "attached to STM32 if prompted"
                )
                session = ConnectHelper.session_with_chosen_probe(
                    target_override=PYOCD_TARGET,
                    user_script=Path(temp_dir) / asic['pyocd-config'],
                )
            else:
                session = ConnectHelper.session_with_chosen_probe(
                    target_override=PYOCD_TARGET,
                    user_script=Path(temp_dir) / asic['pyocd-config'],
                    unique_id=self.adapter_id,
                )
            session.open()
            FileProgrammer(session).program(recovery_hex, format="hex")
            session.board.target.reset_and_halt()
            session.board.target.resume()
            session.close()
            print(f"Successfully flashed {asic['bootfs-name']}")

if __name__ == "__main__":
    main()
