#!/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import time
import subprocess
from pathlib import Path

import pyluwen
import pytest
from twister_harness import DeviceAdapter

from e2e_smoke import (
    dirty_reset_test,
    smi_reset_test,
    arc_watchdog_test,
    pcie_fw_load_time_test,
    get_arc_chip,
)

logger = logging.getLogger(__name__)

SCRIPT_DIR = Path(os.path.dirname(os.path.abspath(__file__)))

# Constant memory addresses we can read from SMC
PING_DMFW_DURATION_REG_ADDR = 0x80030448

# ARC messages
ARC_MSG_TYPE_TEST = 0x90
ARC_MSG_TYPE_PING_DM = 0xC0
ARC_MSG_TYPE_SET_WDT = 0xC1
ARC_MSG_TYPE_READ_TS = 0x1B

# Lower this number if testing local changes, so that tests run faster.
MAX_TEST_ITERATIONS = 1000

# Number of TMON sensors
NUM_TS = 8


def report_results(test_name, fail_count, total_tries):
    """
    Helper function to log the results of a test. This uses a
    consistent format so that twister can parse the results
    """
    logger.info(f"{test_name} completed. Failed {fail_count}/{total_tries} times.")


@pytest.fixture(scope="session")
def arc_chip(unlaunched_dut: DeviceAdapter, asic_id):
    return get_arc_chip(unlaunched_dut, asic_id)


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
    total_tries = min(MAX_TEST_ITERATIONS, 100)
    fail_count = 0
    for i in range(total_tries):
        logger.info(f"Starting ARC watchdog test iteration {i}/{total_tries}")
        result = arc_watchdog_test(arc_chip)
        if not result:
            logger.warning(f"ARC watchdog test failed on iteration {i}")
            fail_count += 1

    report_results("ARC watchdog test", fail_count, total_tries)
    assert fail_count == 0, "ARC watchdog test failed a non-zero number of times."


def test_pcie_fw_load_time(arc_chip):
    """
    Checks PCIe firmware load time is within 40ms.
    This test needs to be run after production reset.
    """
    total_tries = min(MAX_TEST_ITERATIONS, 10)
    fail_count = 0
    for i in range(total_tries):
        logger.info(
            f"Starting PCIe firmware load time test iteration {i}/{total_tries}"
        )
        # Reset the SMC to ensure we have a clean state
        if tt_smi_reset() != 0:
            logger.warning(f"tt-smi reset failed on iteration {i}")
            fail_count += 1
            continue
        result = pcie_fw_load_time_test(arc_chip)
        if not result:
            logger.warning(f"PCIe firmware load time test failed on iteration {i}")
            fail_count += 1

    report_results("PCIe firmware load time test", fail_count, total_tries)
    assert fail_count == 0, (
        "PCIe firmware load time test failed a non-zero number of times."
    )


def test_smi_reset():
    """
    Checks that tt-smi resets are working successfully
    """
    total_tries = min(MAX_TEST_ITERATIONS, 1000)
    fail_count = 0
    dmfw_ping_avg = 0
    dmfw_ping_max = 0
    for i in range(total_tries):
        logger.info(f"Iteration {i}:")
        result = smi_reset_test()

        if not result:
            logger.warning(f"tt-smi reset failed on iteration {i}")
            fail_count += 1
            continue

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

    report_results("tt-smi reset test", fail_count, total_tries)
    assert fail_count == 0, "tt-smi reset test failed a non-zero number of times."


def test_dirty_reset():
    """
    Checks that the SMC comes up correctly after a "dirty" reset, where the
    DMC resets without the SMC requesting it. This is similar to the conditions
    that might be encountered after a NOC hang
    """
    total_tries = min(MAX_TEST_ITERATIONS, 1000)
    fail_count = 0

    for i in range(total_tries):
        logger.info(f"Iteration {i}:")
        result = dirty_reset_test()
        if not result:
            logger.warning(f"dirty reset failed on iteration {i}")
            fail_count += 1
        else:
            logger.info(f"dirty reset passed on iteration {i}")
            # Delay a moment before next run. Without this, tests seem to fail
            # TODO- would be best to determine why rapidly resetting like this
            # breaks enumeration.
            time.sleep(0.5)

    report_results("Dirty reset test", fail_count, total_tries)
    assert fail_count == 0, "Dirty reset failed a non-zero number of times."


def test_dmc_ping(arc_chip):
    """
    Repeatedly pings the DMC from the SMC to see what the average response time
    is. Ping statistics are printed to the log. These statistics are gathered
    without resetting the SMC. The `smi_reset` test will gather statistics
    for the SMC reset case.
    """
    total_tries = min(MAX_TEST_ITERATIONS, 1000)
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
    report_results("DMC ping test", fail_count, total_tries)
    assert fail_count == 0, "DMC ping test failed a non-zero number of times."


def test_power_virus(arc_chip):
    """
    - Read TMON temperatures using MSG_TYPE_READ_TS
    - Run the power virus TTX workload
    - Verify ARC watchdog reset still succeeds while workload runs
    """

    def read_ts_once(chip, sensor_idx: int):
        # ARC handler expects sensor id; returns status in response[1]
        for i in range(NUM_TS):
            rsp = chip.arc_msg(ARC_MSG_TYPE_READ_TS, True, False, i, 0, 5000)
        # Best-effort logging; exact response layout is FW-defined
        logger.info(f"READ_TS idx={sensor_idx} rsp={rsp}")
        # If status is present as second field, ensure success
        if len(rsp) > 1:
            assert rsp[1] == 0, f"READ_TS status error for TS[{sensor_idx}]"
        return rsp

    # Sample TMON before PV
    for _ in range(20):
        for ts in range(0, 8):
            try:
                read_ts_once(arc_chip, ts)
            except Exception as e:
                logger.warning(f"READ_TS pre-PV failed for idx {ts}: {e}")
        time.sleep(0.1)

    # Run tt-burnin for 180s and read_ts at the same time
    logger.info("Starting tt-burnin process for power virus test")
    burnin_process = subprocess.Popen(
        ["tt-burnin"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    duration = 180  # 180 seconds
    fail_count = 0

    try:
        end_time = time.time() + duration
        while time.time() < end_time:
            # Read temperature sensors during burnin
            for ts in range(NUM_TS):
                try:
                    read_ts_once(arc_chip, ts)
                except Exception as e:
                    logger.warning(f"READ_TS during PV failed for idx {ts}: {e}")
                    fail_count += 1

            # Check if burnin process is still running
            if burnin_process.poll() is not None:
                logger.warning("tt-burnin process terminated early")
                break

            time.sleep(1.0)  # Sample every second during power virus

    finally:
        # Stop tt-burnin
        logger.info("Stopping tt-burnin process")
        if burnin_process.poll() is None:
            # Send enter key to stop tt-burnin gracefully
            try:
                burnin_process.stdin.write(b"\n")
                burnin_process.stdin.flush()
                burnin_process.wait(timeout=5)
            except (subprocess.TimeoutExpired, BrokenPipeError):
                logger.warning(
                    "tt-burnin did not terminate gracefully, killing process"
                )
                burnin_process.terminate()
                try:
                    burnin_process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    burnin_process.kill()
                    burnin_process.wait()

    logger.info(
        f"Power virus test completed with {fail_count} temperature read failures"
    )
    assert fail_count == 0, (
        f"Power virus test failed with {fail_count} temperature read failures"
    )
