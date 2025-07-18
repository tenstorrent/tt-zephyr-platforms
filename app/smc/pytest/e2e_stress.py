#!/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import subprocess
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

REFCLK_HZ = 50_000_000

# Constant memory addresses we can read from SMC
ARC_STATUS = 0x80030060
ARC_MISC_CTRL = 0x80030100
PCIE_INIT_CPL_TIME_REG_ADDR = 0x80030438
CMFW_START_TIME_REG_ADDR = 0x8003043C
ARC_START_TIME_REG_ADDR = 0x80030440
PING_DMFW_DURATION_REG_ADDR = 0x80030448

# ARC messages
ARC_MSG_TYPE_TEST = 0x90
ARC_MSG_TYPE_PING_DM = 0xC0
ARC_MSG_TYPE_SET_WDT = 0xC1

# Lower this number if testing local changes, so that tests run faster.
MAX_TEST_ITERATIONS = 1000


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


def tt_smi_reset():
    """
    Resets the SMC using tt-smi
    """
    smi_reset_cmd = "tt-smi -r"
    smi_reset_result = subprocess.run(
        smi_reset_cmd.split(), capture_output=True, check=False
    ).returncode
    return smi_reset_result


def test_arc_watchdog(arc_chip):
    """
    Validates that the DMC firmware watchdog for the ARC will correctly
    reset the chip
    """
    wdt_timeout = 1000
    total_tries = min(MAX_TEST_ITERATIONS, 100)
    for i in range(total_tries):
        logger.info(f"Starting ARC watchdog test iteration {i}/{total_tries}")
        # Setup ARC watchdog for a 1000ms timeout
        arc_chip.arc_msg(ARC_MSG_TYPE_SET_WDT, True, False, wdt_timeout, 0, 1000)
        # Sleep 1500, make sure we can still ping arc
        time.sleep(1.5)
        response = arc_chip.arc_msg(ARC_MSG_TYPE_TEST, True, False, 1, 0, 1000)
        assert response[0] == 2, "SMC did not respond to test message"
        assert response[1] == 0, "SMC response invalid"
        # Halt the ARC cores.
        arc_chip.axi_write32(ARC_MISC_CTRL, 0xF0)
        # Make sure we can still detect ARC core after 500 ms
        time.sleep(0.5)
        arc_chip = pyluwen.detect_chips()[0]
        # Delay 500 more ms. Make sure the ARC has been reset.
        time.sleep(0.5)
        # Ideally we would catch a more narrow exception, but this is what pyluwen raises
        with pytest.raises(Exception):
            # This should fail, since the ARC should have been reset
            arc_chip = pyluwen.detect_chips()[0]
        time.sleep(1.0)
        rescan_pcie()
        # ARC should now be back online
        arc_chip = pyluwen.detect_chips()[0]
        # Make sure ARC can still ping the DMC
        response = arc_chip.arc_msg(ARC_MSG_TYPE_PING_DM, True, False, 0, 0, 1000)
        assert response[0] == 1, "DMC did not respond to ping from SMC"
        assert response[1] == 0, "SMC response invalid"
        logger.info('DMC ping message response "%d"', response[0])


def test_pcie_fw_load_time(arc_chip):
    """
    Checks PCIe firmware load time is within 40ms.
    This test needs to be run after production reset.
    """
    total_tries = min(MAX_TEST_ITERATIONS, 10)
    for i in range(total_tries):
        logger.info(
            f"Starting PCIe firmware load time test iteration {i}/{total_tries}"
        )
        # Reset the SMC to ensure we have a clean state
        assert tt_smi_reset() == 0, "tt-smi reset failed"
        pcie_init_cpl_time = arc_chip.axi_read32(PCIE_INIT_CPL_TIME_REG_ADDR)
        cmfw_start_time = arc_chip.axi_read32(CMFW_START_TIME_REG_ADDR)
        arc_start_time = arc_chip.axi_read32(ARC_START_TIME_REG_ADDR)
        while arc_start_time == 0:
            # Wait for the DMC to report the ARC start time
            logger.info("Waiting for ARC start time to be set...")
            time.sleep(0.1)
            arc_start_time = arc_chip.axi_read32(ARC_START_TIME_REG_ADDR)

        duration_in_ms = (pcie_init_cpl_time - arc_start_time) / REFCLK_HZ * 1000

        logger.info(
            f"BOOTROM start timestamp: {arc_start_time}, "
            f"CMFW start timestamp: {cmfw_start_time}, "
            f"PCIe init completion timestamp: {pcie_init_cpl_time}."
        )

        assert duration_in_ms < 40, f"duration {duration_in_ms:.4f}ms, exceeds 40ms"


def test_smi_reset():
    """
    Checks that tt-smi resets are working successfully
    """
    total_tries = min(MAX_TEST_ITERATIONS, 1000)
    smi_reset_cmd = "tt-smi -r"
    fail_count = 0
    dmfw_ping_avg = 0
    dmfw_ping_max = 0
    for i in range(total_tries):
        logger.info(f"Iteration {i}:")
        smi_reset_result = subprocess.run(
            smi_reset_cmd.split(), capture_output=True, check=False
        ).returncode
        logger.info(f"'tt-smi -r' returncode:{smi_reset_result}")

        if smi_reset_result != 0:
            fail_count += 1

        arc_chip = pyluwen.detect_chips()[0]
        response = arc_chip.arc_msg(ARC_MSG_TYPE_PING_DM, True, False, 0, 0, 1000)
        if response[0] != 1 or response[1] != 0:
            logger.warning(f"Ping failed on iteration {i}")
            fail_count += 1
        duration = arc_chip.axi_read32(PING_DMFW_DURATION_REG_ADDR)
        dmfw_ping_avg += duration / total_tries
        dmfw_ping_max = max(dmfw_ping_max, duration)

    logger.info(
        f"Average DMFW ping time (after reset): {dmfw_ping_avg:.2f} ms, "
        f"Max DMFW ping time (after reset): {dmfw_ping_max:.2f} ms."
    )

    logger.info(f"'tt-smi -r' failed {fail_count}/{total_tries} times.")
    assert fail_count == 0, "'tt-smi -r' failed a non-zero number of times."


def test_dirty_reset():
    """
    Checks that the SMC comes up correctly after a "dirty" reset, where the
    DMC resets without the SMC requesting it. This is similar to the conditions
    that might be encountered after a NOC hang
    """
    total_tries = min(MAX_TEST_ITERATIONS, 1000)
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


def test_dmc_ping(arc_chip):
    """
    Repeatedly pings the DMC from the SMC to see what the average response time
    is. Ping statistics are printed to the log. These statistics are gathered
    without resetting the SMC. The `smi_reset` test will gather statistics
    for the SMC reset case.
    """
    total_tries = 1000
    fail_count = 0
    dmfw_ping_avg = 0
    dmfw_ping_max = 0
    for i in range(total_tries):
        response = arc_chip.arc_msg(ARC_MSG_TYPE_PING_DM, True, False, 0, 0, 1000)
        if response[0] != 1 or response[1] != 0:
            logger.warning(f"Ping failed on iteration {i}")
            fail_count += 1
        duration = arc_chip.axi_read32(PING_DMFW_DURATION_REG_ADDR)
        dmfw_ping_avg += duration / total_tries
        dmfw_ping_max = max(dmfw_ping_max, duration)
    logger.info(
        f"Ping statistics: {total_tries - fail_count} successful pings, "
        f"{fail_count} failed pings."
    )
    # Recalculate the average ping time
    logger.info(
        f"Average DMFW ping time: {dmfw_ping_avg:.2f} ms, "
        f"Max DMFW ping time: {dmfw_ping_max:.2f} ms."
    )
    assert fail_count == 0, f"Ping failed {fail_count}/{total_tries} times."
