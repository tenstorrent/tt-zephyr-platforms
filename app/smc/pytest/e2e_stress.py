#!/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import time
import subprocess
from pathlib import Path

import pyluwen

from e2e_smoke import (
    dirty_reset_test,
    smi_reset_test,
    arc_watchdog_test,
    pcie_fw_load_time_test,
)

# Needed to keep ruff from complaining about this "unused import"
# ruff: noqa: F811
from e2e_smoke import arc_chip_dut, launched_arc_dut  # noqa: F401

logger = logging.getLogger(__name__)

SCRIPT_DIR = Path(os.path.dirname(os.path.abspath(__file__)))

# Constant memory addresses we can read from SMC
PING_DMFW_DURATION_REG_ADDR = 0x80030448

# ARC messages
ARC_MSG_TYPE_TEST = 0x90
ARC_MSG_TYPE_PING_DM = 0xC0
ARC_MSG_TYPE_SET_WDT = 0xC1

# Lower this number if testing local changes, so that tests run faster.
MAX_TEST_ITERATIONS = 1000


def report_results(test_name, fail_count, total_tries):
    """
    Helper function to log the results of a test. This uses a
    consistent format so that twister can parse the results
    """
    logger.info(f"{test_name} completed. Failed {fail_count}/{total_tries} times.")


def tt_smi_reset():
    """
    Resets the SMC using tt-smi
    """
    smi_reset_cmd = "tt-smi -r"
    smi_reset_result = subprocess.run(
        smi_reset_cmd.split(), capture_output=True, check=False
    ).returncode
    return smi_reset_result


def test_arc_watchdog(arc_chip_dut, asic_id):
    """
    Validates that the DMC firmware watchdog for the ARC will correctly
    reset the chip
    """
    # todo: find better way to get test name
    test_name = "ARC watchdog test"
    total_tries = min(MAX_TEST_ITERATIONS, 100)
    fail_count = 0
    failure_fail_count = 0

    for i in range(total_tries):
        if i % 10 == 0:
            logger.info(f"{test_name} iteration {i}/{total_tries}")

        result = arc_watchdog_test(asic_id)
        if not result:
            logger.warning(f"{test_name} failed on iteration {i}")
            fail_count += 1

    report_results(test_name, fail_count, total_tries)
    assert fail_count <= failure_fail_count, (
        f"{test_name} failed {fail_count}/{total_tries} times."
    )


def test_pcie_fw_load_time(arc_chip_dut, asic_id):
    """
    Checks PCIe firmware load time is within 40ms.
    This test needs to be run after production reset.
    """
    # todo: find better way to get test name
    test_name = "PCIe firmware load time test"
    total_tries = min(MAX_TEST_ITERATIONS, 10)
    fail_count = 0
    failure_fail_count = 0

    for i in range(total_tries):
        logger.info(
            f"Starting PCIe firmware load time test iteration {i}/{total_tries}"
        )
        # Reset the SMC to ensure we have a clean state
        if tt_smi_reset() != 0:
            logger.warning(f"tt-smi reset failed on iteration {i}")
            fail_count += 1
            continue
        result = pcie_fw_load_time_test(asic_id)
        if not result:
            logger.warning(f"PCIe firmware load time test failed on iteration {i}")
            fail_count += 1

    report_results(test_name, fail_count, total_tries)
    assert fail_count <= failure_fail_count, (
        f"{test_name} failed {fail_count}/{total_tries} times."
    )


def test_smi_reset(arc_chip_dut, asic_id):
    """
    Checks that tt-smi resets are working successfully
    """
    # todo: find better way to get test name
    test_name = "tt-smi reset test"
    total_tries = min(MAX_TEST_ITERATIONS, 1000)
    fail_count = 0
    failure_fail_count = total_tries // 100
    dmfw_ping_avg = 0
    dmfw_ping_max = 0
    for i in range(total_tries):
        if i % 10 == 0:
            logger.info(f"{test_name} iteration {i}/{total_tries}")

        result = smi_reset_test(asic_id)

        if not result:
            logger.warning(f"tt-smi reset failed on iteration {i}")
            fail_count += 1
            continue

        arc_chip = pyluwen.detect_chips()[asic_id]
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

    report_results(test_name, fail_count, total_tries)
    assert fail_count <= failure_fail_count, (
        f"{test_name} failed {fail_count}/{total_tries} times."
    )


def test_dirty_reset():
    """
    Checks that the SMC comes up correctly after a "dirty" reset, where the
    DMC resets without the SMC requesting it. This is similar to the conditions
    that might be encountered after a NOC hang
    """
    test_name = "Dirty reset test"
    total_tries = min(MAX_TEST_ITERATIONS, 1000)
    fail_count = 0
    failure_fail_count = total_tries // 100

    for i in range(total_tries):
        if i % 10 == 0:
            logger.info(f"{test_name} iteration {i}/{total_tries}")

        result = dirty_reset_test()
        if not result:
            logger.warning(f"dirty reset failed on iteration {i}")
            fail_count += 1
        else:
            # Delay a moment before next run. Without this, tests seem to fail
            # TODO- would be best to determine why rapidly resetting like this
            # breaks enumeration.
            time.sleep(0.5)

    report_results(test_name, fail_count, total_tries)
    assert fail_count <= failure_fail_count, (
        f"{test_name} failed {fail_count}/{total_tries} times."
    )


def test_dmc_ping(arc_chip_dut, asic_id):
    """
    Repeatedly pings the DMC from the SMC to see what the average response time
    is. Ping statistics are printed to the log. These statistics are gathered
    without resetting the SMC. The `smi_reset` test will gather statistics
    for the SMC reset case.
    """
    arc_chip = pyluwen.detect_chips()[asic_id]
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
