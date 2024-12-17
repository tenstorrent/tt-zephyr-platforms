#!/bin/env python3

# Copyright (c) 2024 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import time

import pyluwen
import pytest

from twister_harness import DeviceAdapter

logger = logging.getLogger(__name__)

# Constant memory addresses we can read from SMC
ARC_STATUS = 0x80030060

# ARC messages
ARC_MSG_TYPE_TEST = 0x90
ARC_MSG_TYPE_PING_BM = 0xC0

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
    if dev is None:
        raise RuntimeError("No tenstorrent card found to power off")
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
    with open("/sys/bus/pci/rescan", "w") as f:
        f.write("1")
        time.sleep(1)


@pytest.fixture(scope="session")
def arc_chip(unlaunched_dut: DeviceAdapter):
    """
    Validates the ARC firmware is alive and booted, since this required
    for any test to run
    """
    # This is a hack- the RTT terminal doesn't work in pytest, so
    # we directly call this internal API to flash the DUT.
    unlaunched_dut.generate_command()
    if unlaunched_dut.device_config.extra_test_args:
        unlaunched_dut.command.extend(
            unlaunched_dut.device_config.extra_test_args.split()
        )
    unlaunched_dut._flash_and_run()
    time.sleep(1)
    chips = pyluwen.detect_chips()
    if len(chips) == 0:
        raise RuntimeError("PCIe card was not detected on this system")
    chip = chips[0]
    try:
        status = chip.axi_read32(ARC_STATUS)
    except Exception:
        print("Warning- SMC firmware requires a reset. Rescanning PCIe bus")
        rescan_pcie()
        status = chip.axi_read32(ARC_STATUS)
    assert (status & 0xFFFF0000) == 0xC0DE0000, "SMC firmware postcode is invalid"
    # Check post code status of firmware
    assert (status & 0xFFFF) >= 0x1D, "SMC firmware boot failed"
    return chip


def test_arc_msg(arc_chip):
    """
    Runs a smoke test to verify that the ARC firmware can receive ARC messages
    """
    # Send a test message. We expect response to be incremented by 1
    response = arc_chip.arc_msg(ARC_MSG_TYPE_TEST, True, False, 20, 0, 1000)
    assert response[0] == 21, "SMC did not respond to test message"
    assert response[1] == 0, "SMC response invalid"
    logger.info('SMC ping message response "%d"', response[0])
    # Post code should have updated after first message
    status = arc_chip.axi_read32(ARC_STATUS)
    assert status == 0xC0DE003F, "SMC firmware has incorrect status"


def test_bmc_msg(arc_chip):
    """
    Validates the BMC firmware is alive and responding to pings
    """
    # Send an ARC message to ping the BMC, and validate that it is online
    response = arc_chip.arc_msg(ARC_MSG_TYPE_PING_BM, True, False, 0, 0, 1000)
    assert response[0] == 1, "BMC did not respond to ping from SMC"
    assert response[1] == 0, "SMC response invalid"
    logger.info('BMC ping message response "%d"', response[0])
