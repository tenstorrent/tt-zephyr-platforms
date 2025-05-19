#!/bin/env python3

# Copyright (c) 2024 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import subprocess
import re
import sys
import time
from pathlib import Path

import pyluwen
import pytest
from twister_harness import DeviceAdapter

sys.path.append(str(Path(__file__).parents[3] / "scripts"))
from pcie_utils import rescan_pcie
import dmc_reset

logger = logging.getLogger(__name__)

SCRIPT_DIR = Path(os.path.dirname(os.path.abspath(__file__)))

# Constant memory addresses we can read from SMC
ARC_STATUS = 0x80030060
BOOT_STATUS = 0x80030408

# ARC messages
ARC_MSG_TYPE_TEST = 0x90
ARC_MSG_TYPE_PING_DM = 0xC0


def get_arc_chip(unlaunched_dut: DeviceAdapter):
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
    start = time.time()
    # Attempt to detect the ARC chip for 15 seconds
    timeout = 15
    chips = []
    while True:
        try:
            chips = pyluwen.detect_chips()
        except Exception:
            print("Warning- SMC firmware requires a reset. Rescanning PCIe bus")
        if len(chips) > 0:
            logger.info("Detected ARC chip")
            break
        time.sleep(0.5)
        if time.time() - start > timeout:
            raise RuntimeError("Did not detect ARC chip within timeout period")
        rescan_pcie()
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


@pytest.fixture(scope="session")
def arc_chip(unlaunched_dut: DeviceAdapter):
    return get_arc_chip(unlaunched_dut)


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


def test_dmc_msg(arc_chip):
    """
    Validates the DMC firmware is alive and responding to pings
    """
    # Send an ARC message to ping the DMC, and validate that it is online
    response = arc_chip.arc_msg(ARC_MSG_TYPE_PING_DM, True, False, 0, 0, 1000)
    assert response[0] == 1, "DMC did not respond to ping from SMC"
    assert response[1] == 0, "SMC response invalid"
    logger.info('DMC ping message response "%d"', response[0])


def test_boot_status(arc_chip):
    """
    Validates the boot status of the ARC firmware
    """
    # Read the boot status register and validate that it is correct
    status = arc_chip.axi_read32(BOOT_STATUS)
    assert (status >> 1) & 0x3 == 0x2, "SMC HW boot status is not valid"
    logger.info('SMC boot status "%d"', status)


def test_smbus_status(arc_chip):
    """
    Validates that the SMBUS tests run from the DMC firmware passed
    """
    # We have limited visibility into the DMC firmware, so we read
    # the ARC scratch register the DMC should have set within SMBUS
    # tests to check that they passed
    ARC_SCRATCH_63 = 0x80030400 + (63 * 4)
    status = arc_chip.axi_read32(ARC_SCRATCH_63)
    assert status == 0xFEEDFACE, "SMC firmware did not pass SMBUS tests"
    logger.info('SMC SMBUS status: "0x%x"', status)


def get_int_version_from_file(filename) -> int:
    with open(filename, "r") as f:
        version_data = f.readlines()
    version_dict = {}
    for line in version_data:
        if line:
            # Split the line into key-value pairs
            key_value = line.split("=")
            key = key_value[0].strip()

            if len(key_value) == 2:
                key = key_value[0].strip()
                try:
                    value = int(key_value[1].strip(), 0)
                except ValueError:
                    # Some values are strings
                    value = key_value[1].strip()
                version_dict[key] = value
            else:
                version_dict[key] = None

    if version_dict["EXTRAVERSION"]:
        version_rc = int(re.sub(r"[^\d]", "", version_dict["EXTRAVERSION"]))
    else:
        # version_dict["EXTRAVERSION"] is None or an empty string
        version_rc = 0

    version_int = (
        version_dict["VERSION_MAJOR"] << 24
        | version_dict["VERSION_MINOR"] << 16
        | version_dict["PATCHLEVEL"] << 8
        | version_rc
    )
    return version_int


@pytest.mark.flash
def test_fw_bundle_version(arc_chip):
    """
    Checks that the fw bundle version in telemetry matches the repo
    """
    telemetry = arc_chip.get_telemetry()

    exp_bundle_version = get_int_version_from_file(SCRIPT_DIR.parents[2] / "VERSION")
    assert (
        telemetry.fw_bundle_version == exp_bundle_version
    ), f"Firmware bundle version mismatch: {telemetry.fw_bundle_version:#010x} != {exp_bundle_version:#010x}"
    logger.info(f"FW bundle version: {telemetry.fw_bundle_version:#010x}")


@pytest.mark.flash
def test_smi_reset():
    """
    Checks that tt-smi resets are working successfully
    """
    smi_reset_cmd = "tt-smi -r"
    total_tries = 10
    fail_count = 0
    for i in range(total_tries):
        logger.info(f"Iteration {i}:")
        smi_reset_result = subprocess.run(
            smi_reset_cmd.split(), capture_output=True, check=False
        ).returncode
        logger.info(f"'tt-smi -r' returncode:{smi_reset_result}")

        if smi_reset_result != 0:
            fail_count += 1

    logger.info(f"'tt-smi -r' failed {fail_count}/{total_tries} times.")
    assert fail_count == 0, "'tt-smi -r' failed a non-zero number of times."


@pytest.mark.flash
def test_dirty_reset():
    """
    Checks that the SMC comes up correctly after a "dirty" reset, where the
    DMC resets without the SMC requesting it. This is similar to the conditions
    that might be encountered after a NOC hang
    """
    total_tries = 10
    timeout = 60  # seconds to wait for SMC boot
    fail_count = 0

    # Use dmc-reset script as a library to reset the DMC
    def args():
        return None

    args.config = dmc_reset.DEFAULT_DMC_CFG
    args.debug = 0
    args.openocd = dmc_reset.DEFAULT_OPENOCD
    args.scripts = dmc_reset.DEFAULT_SCRIPTS_DIR
    args.jtag_id = None
    args.hexfile = None
    for i in range(total_tries):
        logger.info(f"Iteration {i}:")
        ret = dmc_reset.reset_dmc(args)
        if ret != os.EX_OK:
            logger.warning(f"DMC reset failed on iteration {i}")
            fail_count += 1
            continue
        ret = dmc_reset.wait_for_smc_boot(timeout)
        if ret != os.EX_OK:
            logger.warning(f"SMC did not boot after dirty reset on iteration {i}")
            fail_count += 1
            continue
        logger.info(f"Iteration {i} of dirty reset test passed")
    logger.info(f"dirty reset failed {fail_count}/{total_tries} times.")
    assert fail_count == 0, "dirty reset failed a non-zero number of times."
