# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import subprocess
import tempfile
import logging
from pathlib import Path
import os
import time

import requests
import pyluwen

from twister_harness import DeviceAdapter
from e2e_smoke import get_int_version_from_file, wait_arc_boot

logger = logging.getLogger(__name__)

SCRIPT_DIR = Path(__file__).parent
ARC_MSG_TYPE_PING_DM = 0xC0


def test_arc_update(unlaunched_dut: DeviceAdapter):
    """
    Validates that the ARC firmware can be updated to BL2 scheme.
    First performs a firmware downgrade to a known good version,
    then performs an upgrade to the BL2 scheme.
    """
    # Wait for the chip to be ready. At entry of this test, the DMFW
    # may be in the process of updating, so wait a bit.
    chip = wait_arc_boot(0, timeout=60)
    # Make sure we can ping the DMC
    response = chip.arc_msg(ARC_MSG_TYPE_PING_DM, True, False, 0, 0, 1000)
    assert response[0] == 1, "DMC did not respond to ping from SMC"
    assert response[1] == 0, "SMC response invalid"

    # URL of the firmware file to download
    firmware_url = "https://github.com/tenstorrent/tt-firmware/raw/e63d1f79371986678ce16738edc74b3d582be956/fw_pack-18.11.0.fwbundle"
    # Expected initial DMC version after downgrade
    DMC_VERSION_INITIAL = (14 << 16) | (0 << 8) | 0  # 14.0.0
    DMC_BL2_VERSION = get_int_version_from_file(
        SCRIPT_DIR.parents[2] / "app" / "dmc" / "VERSION"
    )
    print(f"Expecting DMC version {DMC_BL2_VERSION:x}")

    # Download the firmware file
    try:
        response = requests.get(firmware_url, stream=True, timeout=10)
    except requests.exceptions.Timeout as e:
        logger.error("Request timed out: %s", e)
        raise e
    response.raise_for_status()
    with tempfile.NamedTemporaryFile(delete=False) as temp_firmware:
        for chunk in response.iter_content(chunk_size=8192):
            temp_firmware.write(chunk)
        temp_firmware.close()
        # Flash the firmware using tt-flash
        try:
            result = subprocess.run(
                ["tt-flash", "flash", "--fw-tar", temp_firmware.name, "--force"],
                capture_output=True,
                text=True,
                check=True,
            )
        except subprocess.CalledProcessError as e:
            print("Firmware flash failed")
            print("Return code:", e.returncode)
            print("Output:", e.output)
            print("Error:", e.stderr)
            raise e
        finally:
            os.unlink(temp_firmware.name)  # Clean up the temp file
        if "Error" in result.stdout:
            raise RuntimeError(f"Firmware flash failed {result.stdout}")
        logger.info("Firmware flash output: %s", result.stdout)

    # Firmware flash complete. Now verify that we have the right DMC version.
    chip = pyluwen.detect_chips()[0]
    version = chip.get_telemetry().m3_app_fw_version
    timeout = time.time() + 5  # Wait up to 5 seconds for the version to update
    while version != DMC_VERSION_INITIAL and time.time() < timeout:
        time.sleep(0.5)
        version = chip.get_telemetry().m3_app_fw_version

    assert version == DMC_VERSION_INITIAL, (
        f"Expected initial DMC version {DMC_VERSION_INITIAL:x}, got {version:x}"
    )
    logger.info("DMC firmware downgraded to 0x%x", DMC_VERSION_INITIAL)
    # Make sure we can ping the DMC
    response = chip.arc_msg(ARC_MSG_TYPE_PING_DM, True, False, 0, 0, 1000)
    assert response[0] == 1, "DMC did not respond to ping from SMC"
    assert response[1] == 0, "SMC response invalid"
    # Now perform the upgrade to BL2 scheme
    unlaunched_dut.launch()
    chip = wait_arc_boot(0, timeout=60)
    version = chip.get_telemetry().m3_app_fw_version
    timeout = time.time() + 5  # Wait up to 5 seconds for the version to update
    while version != DMC_BL2_VERSION and time.time() < timeout:
        time.sleep(0.5)
        version = chip.get_telemetry().m3_app_fw_version
    assert version == DMC_BL2_VERSION, (
        f"Expected updated DMC version {DMC_BL2_VERSION:x}, got {version:x}"
    )
    logger.info("DMC firmware upgraded to 0x%x", DMC_BL2_VERSION)
