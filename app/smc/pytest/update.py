# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import subprocess
import tempfile
import logging
from pathlib import Path
import os
import time
import re

import requests
import pyluwen
import pytest

from twister_harness import DeviceAdapter
from e2e_smoke import get_int_version_from_file, wait_arc_boot

logger = logging.getLogger(__name__)

SCRIPT_DIR = Path(__file__).parent
ARC_MSG_TYPE_PING_DM = 0xC0


def flash_recovery_firmware(recovery_url, platform_name):
    # Download the firmware file
    try:
        response = requests.get(recovery_url, stream=True, timeout=10)
    except requests.exceptions.Timeout as e:
        logger.error("Request timed out: %s", e)
        raise e
    response.raise_for_status()

    match = re.search(r".*@(\w\d+\w)\/.*", platform_name)
    # Extract board name from platform string
    if match:
        board_name = match.group(1)
        logger.info("Board name: %s", board_name)
    else:
        raise ValueError(
            f"Could not extract board name from platform string: {platform_name}"
        )

    with tempfile.NamedTemporaryFile(delete=False) as temp_firmware:
        for chunk in response.iter_content(chunk_size=8192):
            temp_firmware.write(chunk)
        temp_firmware.close()
        # Flash the firmware using recovery scripting
        try:
            result = subprocess.run(
                [
                    "python3",
                    str(
                        SCRIPT_DIR.parents[2]
                        / "scripts/tooling/blackhole_recovery/recover-blackhole.py"
                    ),
                    temp_firmware.name,
                    board_name,
                    "--force",
                    "--no-prompt",
                ],
                capture_output=True,
                text=True,
                check=True,
            )
        except subprocess.CalledProcessError as e:
            print("Recovery flash failed")
            print("Return code:", e.returncode)
            print("Output:", e.output)
            print("Error:", e.stderr)
            raise e
        finally:
            os.unlink(temp_firmware.name)  # Clean up the temp file
        logger.info("Recovery flash output: %s", result.stdout)


def flash_stable_fw(url, expected_version):
    # URL of the firmware file to download
    firmware_url = url
    # Expected initial DMC version after downgrade
    DMC_VERSION_INITIAL = expected_version

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
                [
                    "tt-flash",
                    "flash",
                    temp_firmware.name,
                    "--force",
                    "--allow-major-downgrades",
                ],
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


# This test must be first, as it updates the firmware
def test_arc_update(unlaunched_dut: DeviceAdapter):
    """
    Validates that the ARC firmware can be updated to new bootloader scheme.
    First performs a firmware downgrade to a legacy version,
    then performs an upgrade to the new bootloader scheme.
    """
    recovery_url = "https://github.com/tenstorrent/tt-zephyr-platforms/releases/download/v18.12.0-rc1/fw_pack-18.12.0-rc1-recovery.tar.gz"
    stable_url = "https://github.com/tenstorrent/tt-firmware/raw/e63d1f79371986678ce16738edc74b3d582be956/fw_pack-18.11.0.fwbundle"
    DMC_VERSION_INITIAL = (14 << 16) | (0 << 8) | 0  # 14.0.0
    # Flash a recovery firmware to downgrade to legacy scheme
    flash_recovery_firmware(recovery_url, unlaunched_dut.device_config.platform)
    # Wait for the chip to be ready. At entry of this test, the DMFW
    # may be in the process of updating, so wait a bit.
    chip = wait_arc_boot(0, timeout=60)
    # Make sure we can ping the DMC
    response = chip.arc_msg(ARC_MSG_TYPE_PING_DM, True, False, 0, 0, 1000)
    assert response[0] == 1, "DMC did not respond to ping from SMC"
    assert response[1] == 0, "SMC response invalid"
    flash_stable_fw(
        stable_url,
        (14 << 16) | (0 << 8) | 0,  # 14.0.0
    )

    DMC_BL2_VERSION = get_int_version_from_file(
        SCRIPT_DIR.parents[2] / "app" / "dmc" / "VERSION"
    )
    DMC_BL2_VERSION &= 0xFFFFFFF0  # Mask off any rc bits, they won't be reported
    print(f"Expecting DMC version {DMC_BL2_VERSION:x}")

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


def test_mcuboot(unlaunched_dut, asic_id):
    """
    Validates that the SMC falls back to the recovery image
    when the main image is not valid. Only testable when mcuboot is enabled.
    """
    if "p300" in unlaunched_dut.device_config.platform:
        # P300 isn't supported in this test, because tt-flash can't flash in
        # recovery mode on P300 yet. See
        pytest.skip("Skipping test on p300 platform")
    arc_chip = wait_arc_boot(asic_id, timeout=15)
    MCUBOOT_HEADER_ADDR = 0x29E000
    MCUBOOT_MAGIC = 0x96F3B83D
    # First, validate we are running the base image. A good way to check this is
    # to see that telemetry data is available
    try:
        arc_chip.get_telemetry()
    except Exception as e:
        assert False, f"Failed to get telemetry data: {e}"
    # Check that the MCUBOOT header magic is present
    buf = bytes(4)
    arc_chip.as_bh().spi_read(MCUBOOT_HEADER_ADDR, buf)
    magic = int.from_bytes(buf, "little")
    assert magic == MCUBOOT_MAGIC, (
        f"MCUBOOT magic not found at {MCUBOOT_HEADER_ADDR:#010x}"
    )
    logger.info(f"MCUBOOT magic found in main image header: 0x{magic:#010x}")
    # Now, erase the header of the main image, so that the SMC will fall
    # back to the recovery image
    logger.info("Erasing main image header to trigger recovery fallback")
    buf = bytes([0xFF] * 0x1000)
    arc_chip.as_bh().spi_write(MCUBOOT_HEADER_ADDR, buf)
    # Reset the SMC to trigger the fallback
    del arc_chip  # Force re-detection of the chip
    smi_reset_cmd = "tt-smi -r"
    smi_reset_result = subprocess.run(
        smi_reset_cmd.split(), capture_output=True, check=False
    ).returncode
    assert smi_reset_result == 0, "'tt-smi -r' failed"
    arc_chip = wait_arc_boot(asic_id, timeout=15)
    # Validate that the SMC has booted into the recovery image
    with pytest.raises(Exception):
        arc_chip.get_telemetry()
    logger.info("SMC telemetry data not available, as expected in recovery mode")
    # Now, make sure we can flash a good image from recovery mode
    unlaunched_dut.launch()
    del arc_chip  # Force re-detection of the chip
    arc_chip = wait_arc_boot(asic_id, timeout=60)
    # Make sure we can get telemetry data again
    try:
        arc_chip.get_telemetry()
    except Exception as e:
        assert False, f"Failed to get telemetry data after recovery: {e}"
    # Check that the MCUBOOT header magic is present again
    buf = bytes(4)
    arc_chip.as_bh().spi_read(MCUBOOT_HEADER_ADDR, buf)
    magic = int.from_bytes(buf, "little")
