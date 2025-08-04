# Copyright (c) 2024 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import os
import subprocess
import time

from pathlib import Path

TT_PCIE_VID = "0x1e52"


def find_tt_devs():
    """
    Finds PCIe paths for devices to power off
    """
    devs = []
    for root, dirs, _ in os.walk("/sys/bus/pci/devices"):
        for d in dirs:
            with open(os.path.join(root, d, "vendor"), "r") as f:
                vid = f.read()
                if vid.strip() == TT_PCIE_VID:
                    devs.append(os.path.join(root, d))
    return devs


def rescan_pcie():
    """
    Helper to rescan PCIe bus
    """
    # First, we must find the PCIe card to power it off
    devs = find_tt_devs()
    for dev in devs:
        print(f"Powering off device at {dev}")
        remove_path = Path(dev) / "remove"
        try:
            with open(remove_path, "w") as f:
                f.write("1")
        except PermissionError:
            try:
                subprocess.call(f"echo 1 | sudo tee {remove_path}", shell=True)
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
            subprocess.call(f"echo 1 | sudo tee {rescan_path}", shell=True)
            time.sleep(1)
        except Exception as e:
            print("Error, this script must be run with elevated permissions")
            raise e
