# Copyright (c) 2024 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import os
import time


TT_PCIE_VID = "0x1e52"


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
        try:
            with open(os.path.join(dev, "remove"), "w") as f:
                f.write("1")
        except PermissionError as e:
            print(
                "Error, this script must be run with elevated permissions to rescan PCIe bus"
            )
            raise e
    # Now, rescan the bus
    try:
        with open("/sys/bus/pci/rescan", "w") as f:
            f.write("1")
            time.sleep(1)
    except PermissionError as e:
        print(
            "Error, this script must be run with elevated permissions to rescan PCIe bus"
        )
        raise e
