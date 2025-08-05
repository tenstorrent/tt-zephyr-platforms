#!/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import argparse
import os
import sys
import subprocess
import time

try:
    import pyluwen
    import yaml
    from pyocd.core.helpers import ConnectHelper
    from pyocd.flash.file_programmer import FileProgrammer
except ImportError:
    print("Required modules not found. Please run pip install -r requirements.txt")
    sys.exit(os.EX_UNAVAILABLE)

BR_BASE = Path(__file__).parent.absolute()
DEFAULT_CONFIG_YAML = BR_BASE / "config.yaml"

def parse_args():
    parser = argparse.ArgumentParser(description="Recover a blackhole PCIe card.")
    parser.add_argument("board", type=str,
            help="Product name of the blackhole card (e.g., 'p100a', 'p150a').")
    parser.add_argument("--pci-idx", type=int, default=0,
            help="PCI index of the card (default: 0).")
    parser.add_argument("--adapter-id", type=str,
            help="Adapter id for the ST-Link device used in recovery")
    parser.add_argument("--config", type=Path, default=f"{DEFAULT_CONFIG_YAML}",
            help="Path to recovery configuration file (default: 'config.yaml').")
    return parser.parse_args()

TT_PCIE_VID = "0x1e52"
ARC_PING_MSG = 0x90
DMC_PING_MSG = 0xC0

def find_tt_bus():
    """
    Finds PCIe path for device to power off
    """
    for root, dirs, _ in os.walk("/sys/bus/pci/devices"):
        for d in dirs:
            with open(os.path.join(root, d, "vendor"), "r") as f:
                vid = f.read()
                if vid.strip() == TT_PCIE_VID:
                    return os.path.join(root, d)
    return None


def rescan_pcie():
    """
    Helper to rescan PCIe bus
    """
    # First, we must find the PCIe card to power it off
    dev = find_tt_bus()
    if dev is not None:
        print(f"Powering off device at {dev}")
        remove_path = Path(dev) / "remove"
        try:
            with open(remove_path, "w") as f:
                f.write("1")
        except PermissionError:
            try:
                subprocess.call(f"echo 1 | sudo tee {remove_path} > /dev/null", shell=True)
            except Exception as e:
                print("Error, this script must be run with elevated permissions")
                raise e

    # Now, rescan the bus
    rescan_path = Path("/sys/bus/pci/rescan")
    try:
        with open(rescan_path, "w") as f:
            f.write("1")
            time.sleep(1)
    except PermissionError:
        try:
            subprocess.call(f"echo 1 | sudo tee {rescan_path} > /dev/null", shell=True)
            time.sleep(1)
        except Exception as e:
            print("Error, this script must be run with elevated permissions")
            raise e

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
        if card.get_telemetry().board_id >> 36 != config['upi']:
            return False
    except BaseException:
        return False
    return True

def reset_dmc(target, pci_idx, delay = 1):
    """Helper to reset the DMC and rescan PCI"""
    target.reset_and_halt()
    target.resume()
    # Rescan PCIe repeatedly until delay expires
    timeout = time.time() + delay
    while time.time() < timeout:
        # Throttle the requests to rescan PCIe
        time.sleep(1)
        rescan_pcie()

def main():
    args = parse_args()
    if not args.config.is_file():
        raise FileNotFoundError(f"Configuration file '{args.config}' not found.")
    with open(args.config, 'r') as config_file:
        config = yaml.safe_load(config_file)
    if args.board not in config:
        print(f"Board '{args.board}' not found in configuration.")
        print("Available boards:", ", ".join(config.keys()))
        return os.EX_USAGE
    board_config = config[args.board]

    for key in ['dmc_fw', 'smc_fw', 'board_id_data', 'pyocd_config', 'dmc_dfp']:
        if key not in board_config:
            print(f"Missing required key '{key}' in configuration for board '{args.board}'.")
            return os.EX_CONFIG
        if not isinstance(board_config[key], str):
            print(f"Configuration value for '{key}' must be a string, got {type(board_config[key])}.")
            return os.EX_CONFIG
        if not Path(board_config[key]).is_absolute():
            board_config[key] = str(BR_BASE / board_config[key])

    # Check if the card is already in a good state
    if check_card_status(args.pci_idx, board_config):
        print("Card already appears functional, exiting")
        return os.EX_OK

    print("Phase 1: Rescanning PCIe bus")
    rescan_pcie()
    if check_card_status(args.pci_idx, board_config):
        print("Card appears functional after rescan, exiting")
        return os.EX_OK

    print("Phase 2: Programming DMC firmware")
    session_options = {
        "target_override": "STM32G0B1CEUx",
        "user_script": board_config['pyocd_config'],
        "pack": [board_config['dmc_dfp']],
    }
    if args.adapter_id is None:
        print("No adapter ID provided, please select the ST-Link device if prompted")
        session = ConnectHelper.session_with_chosen_probe(
            target_override=session_options['target_override'],
            user_script=session_options['user_script'],
            pack=session_options['pack'],
        )
    else:
        session = ConnectHelper.session_with_chosen_probe(
            target_override=session_options['target_override'],
            user_script=session_options['user_script'],
            pack=session_options['pack'],
            unique_id=args.adapter_id,
        )
    session.open()
    target = session.board.target
    # Program the DMC firmware
    FileProgrammer(session).program(board_config['dmc_fw'])
    reset_dmc(target, args.pci_idx)
    if check_card_status(args.pci_idx, board_config):
        print("Card appears functional after DMC firmware programming, exiting")
        session.close()
        return os.EX_OK

    print("Phase 3: Rewriting EEPROM with SMC and DMC firmware")
    FileProgrammer(session).program(board_config['smc_fw'])
    reset_dmc(target, args.pci_idx)
    if check_card_status(args.pci_idx, board_config):
        print("Card appears functional after SMC firmware programming, exiting")
        session.close()
        return os.EX_OK

    print("Phase 4: Resetting DMC and waiting for firmware update")
    # For this reset, the DMC may trigger a firmware update, so we need to wait
    # for a bit so it can complete
    print("Waiting 60 seconds for the DMC in case it triggers a firmware update")
    reset_dmc(target, args.pci_idx, delay=60)
    if check_card_status(args.pci_idx, board_config):
        print("Card appears functional after DMC update, exiting")
        session.close()
        return os.EX_OK

    print("Phase 5: Writing EEPROM with default UPI configuration")
    FileProgrammer(session).program(board_config['board_id_data'])
    reset_dmc(target, args.pci_idx, delay=5)
    if check_card_status(args.pci_idx, board_config):
        print("Card appears functional, but your UPI was rewritten. Please contact support for assistance.")
        session.close()
        return os.EX_OK

    session.close()
    print("All recovery phases failed- please contact support")
    return os.EX_SOFTWARE

if __name__ == "__main__":
    sys.exit(main())
