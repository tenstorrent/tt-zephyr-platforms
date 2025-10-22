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
from urllib.request import urlretrieve

import pyluwen
import pytest
import get_ttzp_version


def strip_ansi_codes(s):
    ansi_escape = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")
    return ansi_escape.sub("", s)


try:
    from twister_harness import DeviceAdapter
    from twister_harness.exceptions import (
        TwisterHarnessException,
        TwisterHarnessTimeoutException,
    )
except ImportError:
    # Fallback if twister_harness is not available
    print("Warning: twister_harness module not found. Using fallback harness")

    class TwisterHarnessException(Exception):
        pass

    class TwisterHarnessTimeoutException(Exception):
        pass

    class DeviceAdapter:
        def __init__(self, fwbundle=None):
            self.fwbundle = fwbundle

        def launch(self):
            result = subprocess.run(
                ["tt-flash", "flash", str(self.fwbundle), "--force"],
                capture_output=True,
                text=True,
            )

            assert "FLASH SUCCESS" in strip_ansi_codes(result.stdout)

    @pytest.fixture(scope="session")
    def unlaunched_dut(fwbundle):
        return DeviceAdapter(fwbundle)


TTZP = Path(__file__).parents[3]
sys.path.append(str(TTZP / "scripts"))
from pcie_utils import rescan_pcie  # noqa: E402
import smc_test_recovery  # noqa: E402
import dmc_reset  # noqa: E402

logger = logging.getLogger(__name__)

SCRIPT_DIR = Path(os.path.dirname(os.path.abspath(__file__)))

REFCLK_HZ = 50_000_000

# Constant memory addresses we can read from SMC
ARC_STATUS = 0x80030060
ARC_MISC_CTRL = 0x80030100
BOOT_STATUS = 0x80030408
PCIE_INIT_CPL_TIME_REG_ADDR = 0x80030438
CMFW_START_TIME_REG_ADDR = 0x8003043C
ARC_START_TIME_REG_ADDR = 0x80030440
ARC_HANG_PC_REG_ADDR = 0x80030454
TELEMETRY_DATA_REG_ADDR = 0x80030430

# ARC messages
ARC_MSG_TYPE_REINIT_TENSIX = 0x20
ARC_MSG_TYPE_FORCE_AICLK = 0x33
ARC_MSG_TYPE_GET_AICLK = 0x34
ARC_MSG_TYPE_TEST = 0x90
ARC_MSG_TYPE_TOGGLE_TENSIX_RESET = 0xAF
ARC_MSG_TYPE_PING_DM = 0xC0
ARC_MSG_TYPE_SET_WDT = 0xC1

# Telemetry tags
TAG_CM_FW_VERSION = 29
TAG_DM_APP_FW_VERSION = 26


def read_telem(asic_id, telem_idx):
    chip = pyluwen.detect_chips()[asic_id]

    table_addr = chip.axi_read32(TELEMETRY_DATA_REG_ADDR)
    telem = chip.axi_read32(table_addr + telem_idx * 4)

    del chip

    return telem


@pytest.fixture(scope="session")
def launched_arc_dut(unlaunched_dut: DeviceAdapter):
    """
    This fixture launches the Zephyr DUT once per test session, and returns
    a reference to the launched DUT. This is used by tests that need to
    flash the DUT once, and then run multiple tests against it.
    """
    logger.info("Flashing ARC DUT")
    try:
        unlaunched_dut.launch()
    except TwisterHarnessException:
        pytest.exit("Failed to flash DUT")
    except TwisterHarnessTimeoutException:
        pytest.exit("DUT flash timed out")
    time.sleep(1)  # Wait for ARC to start
    return unlaunched_dut


def wait_arc_boot(asic_id, timeout=15):
    start = time.time()
    # Attempt to detect the ARC chip for 15 seconds
    timeout = timeout
    while True:
        try:
            chips = pyluwen.detect_chips()
            if len(chips) > asic_id:
                logger.info("Detected ARC chip")
                break
        except Exception:
            logger.warning("SMC firmware requires a reset. Rescanning PCIe bus")
        except BaseException as e:
            # We will continue through these exceptions, since pyluwen
            # sometimes throws rust exceptions when the chip is resetting.
            # log them with a higher severity so we can track them
            logger.error(f"Base exception error while detecting chips: {e}")
        time.sleep(0.5)
        if time.time() - start > timeout:
            # Dump the SMC state for debugging
            smc_test_recovery.recover_smc(asic_id)
            logger.error("Did not detect ARC chip within timeout period")
            pytest.exit("Did not detect ARC chip within timeout period")
        rescan_pcie()
    chip = chips[asic_id]
    try:
        status = chip.axi_read32(ARC_STATUS)
    except Exception:
        logger.warning("SMC firmware requires a reset. Rescanning PCIe bus")
        rescan_pcie()
        status = chip.axi_read32(ARC_STATUS)
    assert (status & 0xFFFF0000) == 0xC0DE0000, "SMC firmware postcode is invalid"
    # Check post code status of firmware
    assert (status & 0xFFFF) >= 0x1D, "SMC firmware boot failed"
    # Remove references to chip objects so pyluwen will close file descriptors.
    # Otherwise these may become stale when SMC resets.
    return chips[asic_id]


@pytest.fixture()
def arc_chip_dut(launched_arc_dut, asic_id):
    """
    This fixture returns the Zephyr DUT once the ARC chip is ready. Otherwise, it
    raises a runtime error.

    We use this approach rather than providing a reference to the pyluwen
    chip directly because tests may need to delete chip objects after they
    reset the ARC chip to induce pyluwen to close open file descriptors
    """
    chip = wait_arc_boot(asic_id, timeout=15)
    del chip  # So we don't hold stale file descriptors
    return launched_arc_dut


def upgrade_from_version_test(
    arc_chip_dut,
    tmp_path: Path,
    board_name,
    unlaunched_dut,
    version,
    dmfw_version_base,
    cmfw_version_base,
):
    if board_name is None:
        pytest.skip("Upgrade test requires --board set, skipping upgrade test")

    RECOVERY_URL = "https://github.com/tenstorrent/tt-zephyr-platforms/releases/download/v18.12.0-rc1/fw_pack-18.12.0-rc1-recovery.tar.gz"

    tar_recovery = tmp_path / "recovery.tar.gz"
    if not tar_recovery.exists():
        urlretrieve(RECOVERY_URL, tar_recovery)
    recovery_py = str(
        Path(__file__).parents[3]
        / "scripts/tooling/blackhole_recovery/recover-blackhole.py"
    )
    # versions we want to check upgrading from

    # flash recovery first
    try:
        subprocess.check_call(
            [
                "python3",
                recovery_py,
                tar_recovery,
                board_name,
                "--force",
                "--no-prompt",
            ]
        )
    except subprocess.CalledProcessError as e:
        logger.error(f"blackhole_recovery.py failed with error: {e}")
        assert False

    # flash "base" firmware to update from
    URL = f"https://github.com/tenstorrent/tt-firmware/releases/download/v{version}/fw_pack-{version}.fwbundle"
    targz = tmp_path / "fw_pack.tar.gz"

    urlretrieve(URL, targz)
    try:
        result = subprocess.run(
            ["tt-flash", "flash", "--fw-tar", str(targz), "--force"],
            capture_output=True,
            text=True,
        )

        assert "FLASH SUCCESS" in strip_ansi_codes(result.stdout)

    except subprocess.CalledProcessError as e:
        logger.error(
            f"tt-flash flash --fw_tar {targz} --force: failed with error: {e}\n{result.stdout}"
        )
        assert False

    time.sleep(0.5)
    assert dmfw_version_base == read_telem(0, TAG_DM_APP_FW_VERSION)
    assert cmfw_version_base == read_telem(0, TAG_CM_FW_VERSION)

    # flash firmware to update to
    unlaunched_dut.launch()

    time.sleep(0.5)
    assert get_ttzp_version.get_ttzp_version_u32(
        TTZP / "app/dmc/VERSION"
    ) == read_telem(0, TAG_DM_APP_FW_VERSION)
    assert get_ttzp_version.get_ttzp_version_u32(
        TTZP / "app/smc/VERSION"
    ) == read_telem(0, TAG_CM_FW_VERSION)


def test_upgrade_from_18_10(arc_chip_dut, tmp_path: Path, board_name, unlaunched_dut):
    upgrade_from_version_test(
        arc_chip_dut,
        tmp_path,
        board_name,
        unlaunched_dut,
        "18.10.0",
        (13 << 16),
        (19 << 16),
    )


def test_upgrade_from_19_00(board_name, unlaunched_dut):
    recovery_py = str(
        Path(__file__).parents[3]
        / "scripts/tooling/blackhole_recovery/recover-blackhole.py"
    )
    # flash custom recovery bundle
    try:
        subprocess.check_call(
            [
                "python3",
                recovery_py,
                str(TTZP / "zephyr/blobs/recovery-19.0-custom.tar.gz"),
                board_name,
                "--force",
                "--no-prompt",
            ]
        )
    except subprocess.CalledProcessError as e:
        logger.error(f"blackhole_recovery.py failed with error: {e}")
        assert False
    time.sleep(0.5)
    assert (15 << 16) == read_telem(0, TAG_DM_APP_FW_VERSION)
    assert (21 << 16) == read_telem(0, TAG_CM_FW_VERSION)

    # We now need to workaround the copy of MCUBoot in the recovery image using
    # a different signature from the image we have built in the test. To do this,
    # flash the mcuboot-bl2 image from our build directory directly using west
    try:
        subprocess.check_call(
            [
                "west",
                "flash",
                "--skip-rebuild",
                "-d",
                str(unlaunched_dut.device_config.app_build_dir.parent / "mcuboot-bl2"),
            ]
        )
    except subprocess.CalledProcessError as e:
        logger.error(f"west flash failed with error: {e}")
        assert False
    # Wait for the ARC chip to boot
    wait_arc_boot(0, timeout=20)

    # flash firmware to update to
    unlaunched_dut.launch()
    time.sleep(0.5)
    assert get_ttzp_version.get_ttzp_version_u32(
        TTZP / "app/dmc/VERSION"
    ) == read_telem(0, TAG_DM_APP_FW_VERSION)
    assert get_ttzp_version.get_ttzp_version_u32(
        TTZP / "app/smc/VERSION"
    ) == read_telem(0, TAG_CM_FW_VERSION)


def test_arc_msg(arc_chip_dut, asic_id):
    """
    Runs a smoke test to verify that the ARC firmware can receive ARC messages
    """
    # Send a test message. We expect response to be incremented by 1
    arc_chip = pyluwen.detect_chips()[asic_id]
    response = arc_chip.arc_msg(ARC_MSG_TYPE_TEST, True, False, 20, 0, 1000)
    assert response[0] == 21, "SMC did not respond to test message"
    assert response[1] == 0, "SMC response invalid"
    logger.info('SMC ping message response "%d"', response[0])
    # Post code should have updated after first message
    status = arc_chip.axi_read32(ARC_STATUS)
    assert status == 0xC0DE003F, "SMC firmware has incorrect status"


def test_dmc_msg(arc_chip_dut, asic_id):
    """
    Validates the DMC firmware is alive and responding to pings
    """
    # Send an ARC message to ping the DMC, and validate that it is online
    arc_chip = pyluwen.detect_chips()[asic_id]
    response = arc_chip.arc_msg(ARC_MSG_TYPE_PING_DM, True, False, 0, 0, 1000)
    assert response[0] == 1, "DMC did not respond to ping from SMC"
    assert response[1] == 0, "SMC response invalid"
    logger.info('DMC ping message response "%d"', response[0])


def test_boot_status(arc_chip_dut, asic_id):
    """
    Validates the boot status of the ARC firmware
    """
    # Read the boot status register and validate that it is correct
    arc_chip = pyluwen.detect_chips()[asic_id]
    status = arc_chip.axi_read32(BOOT_STATUS)
    assert (status >> 1) & 0x3 == 0x2, "SMC HW boot status is not valid"
    logger.info('SMC boot status "%d"', status)


def test_smbus_status(arc_chip_dut, asic_id):
    """
    Validates that the SMBUS tests run from the DMC firmware passed
    """
    # We have limited visibility into the DMC firmware, so we read
    # the ARC scratch register the DMC should have set within SMBUS
    # tests to check that they passed
    arc_chip = pyluwen.detect_chips()[asic_id]
    ARC_SCRATCH_63 = 0x80030400 + (63 * 4)
    status = arc_chip.axi_read32(ARC_SCRATCH_63)
    assert status == 0xFEEDFACE, "SMC firmware did not pass SMBUS tests"
    logger.info('SMC SMBUS status: "0x%x"', status)


def test_flash_write(arc_chip_dut, asic_id):
    """
    Validates that flash read/write works via pyluwen,
    since this is the same interface used by tt-flash
    """
    SPI_RX_TRAIN_ADDR = 0x13FFC
    SPI_RX_TRAIN_DATA = 0xA5A55A5A
    SCRATCH_REGION = 0x2800000
    SCRATCH_REGION_SIZE = 0x10000
    WRITE_SIZE = 0x8000
    NUM_ITERATIONS = 5

    arc_chip = pyluwen.detect_chips()[asic_id]
    # Read the SPI RX training region and validate that it is correct
    data = bytes(4)
    check_data = SPI_RX_TRAIN_DATA.to_bytes(4, byteorder="little")
    arc_chip.as_bh().spi_read(SPI_RX_TRAIN_ADDR, data)
    assert data == check_data, "SMC SPI RX training region is not valid"
    logger.info(
        f"SPI RX training region: 0x{int.from_bytes(data, byteorder='little'):x}"
    )

    # To best simulate the write pattern of tt-flash, write large chunks
    for i in range(NUM_ITERATIONS):
        for addr in range(
            SCRATCH_REGION, SCRATCH_REGION + SCRATCH_REGION_SIZE, WRITE_SIZE
        ):
            data = bytes(os.urandom(WRITE_SIZE))
            check_data = bytes(WRITE_SIZE)
            arc_chip.as_bh().spi_write(addr, data)
            arc_chip.as_bh().spi_read(addr, check_data)

            assert data == check_data, "SMC SPI write failed"
            logger.info(f"Write to scratch region: 0x{addr:x} passed")
        logger.info("Flash test %d of %d passed", i + 1, NUM_ITERATIONS)


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


def arc_watchdog_test(asic_id):
    """
    Helper to run ARC watchdog test. Returns True on pass, or False on
    failure
    """
    arc_chip = pyluwen.detect_chips()[asic_id]
    wdt_timeout = 1000
    # Setup ARC watchdog for a 1000ms timeout
    arc_chip.arc_msg(ARC_MSG_TYPE_SET_WDT, True, False, wdt_timeout, 0, 1000)
    # Sleep 1500, make sure we can still ping arc
    time.sleep(1.5)
    response = arc_chip.arc_msg(ARC_MSG_TYPE_TEST, True, False, 1, 0, 1000)
    if response[0] != 2:
        logger.warning("SMC did not respond to test message")
        return False
    if response[1] != 0:
        logger.warning("SMC response invalid")
        return False
    # Clear the ARC hang register- this should be set during a watchdog reset
    arc_chip.axi_write32(ARC_HANG_PC_REG_ADDR, 0)
    # Halt the ARC cores.
    arc_chip.axi_write32(ARC_MISC_CTRL, 0xF0)
    # Make sure we can still detect ARC core after 500 ms
    time.sleep(0.5)
    try:
        arc_chip = pyluwen.detect_chips()[asic_id]
        hang_pc = arc_chip.axi_read32(ARC_HANG_PC_REG_ADDR)
        if hang_pc != 0:
            logger.error(
                "DMC reset too early (hang PC set), ARC should still be online"
            )
            return False
    except Exception:
        # Ideally we would catch a more specific exception here, but pyluwen
        # does not currently have specific exception types
        logger.error("DMC reset too early, ARC should still be online")
        return False
    # Delay 600 more ms. Make sure the ARC has been reset.
    time.sleep(0.6)
    # Delete arc chip object, to make sure pyluwen drops open file descriptors
    del arc_chip
    try:
        arc_chip = pyluwen.detect_chips()[asic_id]
        hang_pc = arc_chip.axi_read32(ARC_HANG_PC_REG_ADDR)
        # If the ARC chip was reset, the hang program counter should have been set
        if hang_pc == 0:
            logger.warning(
                "ARC did not reset, waiting 10 additional seconds to see if ARC core resets"
            )
            time.sleep(10)
            rescan_pcie()
            arc_chip = pyluwen.detect_chips()[asic_id]
            hang_pc = arc_chip.axi_read32(ARC_HANG_PC_REG_ADDR)
            if hang_pc == 0:
                logger.error("ARC core was not reset after ten seconds")
                return False
    except Exception:
        # We expect an exception here since the ARC should be resetting
        pass
    except BaseException as e:
        # We will continue through these exceptions, since pyluwen
        # sometimes throws rust exceptions when the chip is resetting.
        # log them with a higher severity so we can track them
        logger.error(f"Base exception error while detecting chips: {e}")
    # Delay a bit, then rescan PCIe
    time.sleep(1.0)
    # Rescan PCIe, and see if ARC chip has been reset
    rescan_pcie()
    arc_chip = pyluwen.detect_chips()[asic_id]
    hang_pc = arc_chip.axi_read32(ARC_HANG_PC_REG_ADDR)
    if hang_pc == 0:
        logger.error("ARC core was not reset, but PCIe device re-enumerated?")
        return False
    logger.info(f"ARC was reset, hang PC 0x{hang_pc:08X}")
    # Make sure ARC can still ping the DMC
    response = arc_chip.arc_msg(ARC_MSG_TYPE_PING_DM, True, False, 0, 0, 1000)
    if response[0] != 1:
        logger.warning("DMC did not respond to ping after reset")
        return False
    if response[1] != 0:
        logger.warning("SMC response invalid after reset")
        return False
    logger.info('DMC ping message response "%d"', response[0])
    return True


def test_arc_watchdog(arc_chip_dut, asic_id):
    """
    Validates that the DMC firmware watchdog for the ARC will correctly
    reset the chip
    """
    assert arc_watchdog_test(asic_id), "ARC watchdog test failed"


def pcie_fw_load_time_test(asic_id):
    """
    Helper to run PCIe FW load time test. Returns True if passed, False if failed
    """
    arc_chip = pyluwen.detect_chips()[asic_id]
    duration_deadline = 40  # 40 ms.
    pcie_init_cpl_time = arc_chip.axi_read32(PCIE_INIT_CPL_TIME_REG_ADDR)
    cmfw_start_time = arc_chip.axi_read32(CMFW_START_TIME_REG_ADDR)
    arc_start_time = arc_chip.axi_read32(ARC_START_TIME_REG_ADDR)
    # Wait up to 5 seconds for DMC to boot and sync with ARC
    timeout = time.time() + 5
    while arc_start_time == 0 and time.time() < timeout:
        # Wait for the DMC to report the ARC start time
        logger.info("Waiting for ARC start time to be set...")
        time.sleep(0.1)
        arc_start_time = arc_chip.axi_read32(ARC_START_TIME_REG_ADDR)

    if arc_start_time == 0:
        logger.warning("DMC never reported ARC start time")
        return False

    duration_in_ms = (pcie_init_cpl_time - arc_start_time) / REFCLK_HZ * 1000

    logger.info(
        f"BOOTROM start timestamp: {arc_start_time}, "
        f"CMFW start timestamp: {cmfw_start_time}, "
        f"PCIe init completion timestamp: {pcie_init_cpl_time}."
    )

    if duration_in_ms > duration_deadline:
        logger.warning(
            f"PCIe firmware load time exceeded {duration_deadline}ms: {duration_in_ms:.4f}ms"
        )
        return False

    return True


def test_pcie_fw_load_time(arc_chip_dut, asic_id):
    """
    Checks PCIe firmware load time is within 40ms.
    This test needs to be run after production reset.
    """
    assert pcie_fw_load_time_test(asic_id), "PCIe firmware load time test failed"


def test_fw_bundle_version(arc_chip_dut, asic_id):
    """
    Checks that the fw bundle version in telemetry matches the repo
    """
    arc_chip = pyluwen.detect_chips()[asic_id]
    telemetry = arc_chip.get_telemetry()

    exp_bundle_version = get_int_version_from_file(SCRIPT_DIR.parents[2] / "VERSION")
    assert telemetry.fw_bundle_version == exp_bundle_version, (
        f"Firmware bundle version mismatch: {telemetry.fw_bundle_version:#010x} != {exp_bundle_version:#010x}"
    )
    logger.info(f"FW bundle version: {telemetry.fw_bundle_version:#010x}")


def smi_reset_test(asic_id):
    """
    Helper to run tt-smi reset test. Returns True if test passed, False otherwise
    """
    smi_reset_cmd = "tt-smi -r"
    smi_reset_result = subprocess.run(
        smi_reset_cmd.split(), capture_output=True, check=False
    )
    if smi_reset_result.returncode != 0:
        logger.warning(f"'tt-smi -r' failed: {smi_reset_result.stdout.decode()}")
        smc_test_recovery.recover_smc(asic_id)

    return smi_reset_result.returncode == 0


def test_smi_reset(arc_chip_dut, asic_id):
    """
    Checks that tt-smi resets are working successfully
    """
    total_tries = 10
    fail_count = 0
    for i in range(total_tries):
        logger.info(f"Iteration {i}:")
        result = smi_reset_test(asic_id)

        if not result:
            logger.warning(f"tt-smi reset failed on iteration {i}")
            fail_count += 1

    logger.info(f"'tt-smi -r' failed {fail_count}/{total_tries} times.")
    assert fail_count == 0, "'tt-smi -r' failed a non-zero number of times."


def dirty_reset_test():
    """
    Helper to execute dirty reset test. Returns True if test passes, False otherwise
    """
    timeout = 60  # seconds to wait for SMC boot

    # Use dmc-reset script as a library to reset the DMC
    def args():
        return None

    args.config = dmc_reset.DEFAULT_DMC_CFG
    args.debug = 0
    args.openocd = dmc_reset.DEFAULT_OPENOCD
    args.scripts = dmc_reset.DEFAULT_SCRIPTS_DIR
    args.jtag_id = None
    args.hexfile = None

    ret = dmc_reset.reset_dmc(args)
    if ret != os.EX_OK:
        logger.warning("DMC reset failed on iteration")
        return False
    ret = dmc_reset.wait_for_smc_boot(timeout)
    if ret != os.EX_OK:
        logger.warning("SMC did not boot after dirty reset")
        return False
    return True


def test_dirty_reset():
    """
    Checks that the SMC comes up correctly after a "dirty" reset, where the
    DMC resets without the SMC requesting it. This is similar to the conditions
    that might be encountered after a NOC hang
    """
    total_tries = 10
    fail_count = 0

    for i in range(total_tries):
        logger.info(f"Iteration {i}:")
        result = dirty_reset_test()
        if not result:
            logger.warning(f"dirty reset failed on iteration {i}")
            fail_count += 1
        else:
            logger.info(f"dirty reset passed on iteration {i}")

    logger.info(f"dirty reset failed {fail_count}/{total_tries} times.")
    assert fail_count == 0, "dirty reset failed a non-zero number of times."


def tensix_reset_sequence(arc_chip):
    """
    Careful sequence to reset all the Tensixes
    """
    TENSIX_RISC_RESET_ADDR = [0x80030040 + i * 4 for i in range(8)]
    SOFT_RESET_ADDR = 0xFFB121B0
    SOFT_RESET_DATA = 0x47800

    # Force AICLK to safe frequency
    arc_chip.arc_msg(ARC_MSG_TYPE_FORCE_AICLK, arg0=250, arg1=0)
    # Clear RISC reset registers
    for addr in TENSIX_RISC_RESET_ADDR:
        arc_chip.axi_write32(addr, 0)

    # The tensix reset message
    response = arc_chip.arc_msg(ARC_MSG_TYPE_TOGGLE_TENSIX_RESET)
    assert response[1] == 0, "SMC response invalid to toggle tensix reset message"
    response = arc_chip.arc_msg(ARC_MSG_TYPE_REINIT_TENSIX)
    assert response[1] == 0, "SMC response invalid to reinit tensix message"

    # Set soft reset registers inside Tensix
    arc_chip.noc_broadcast32(1, SOFT_RESET_ADDR, SOFT_RESET_DATA)

    # Release RISC reset registers
    for addr in TENSIX_RISC_RESET_ADDR:
        arc_chip.axi_write32(addr, 0xFFFFFFFF)

    # Unforce AICLK
    arc_chip.arc_msg(ARC_MSG_TYPE_FORCE_AICLK, arg0=0, arg1=0)


def test_tensix_reset(arc_chip_dut, asic_id):
    """
    Validates the Tensix reset sequence
    """
    # Unused register in Tensix. Use bit 0 as a scratch bit
    # "PREFECTH" is a typo carried over from the register name in the RTL
    arc_chip = pyluwen.detect_chips()[asic_id]
    ETH_RISC_PREFECTH_CTRL_ADDR = 0xFFB120B8
    # This Tensix coordinate (1-2) should be available in all current harvesting configs
    scratch_set = arc_chip.noc_read32(
        noc_id=0, x=1, y=2, addr=ETH_RISC_PREFECTH_CTRL_ADDR
    )
    scratch_set |= 1  # Set bit 0 as a scratch flag

    for i in range(10):
        logger.info(f"Starting Tensix reset test iteration {i}")
        # Set scratch bit before reset
        arc_chip.noc_write32(
            noc_id=0, x=1, y=2, addr=ETH_RISC_PREFECTH_CTRL_ADDR, data=scratch_set
        )

        tensix_reset_sequence(arc_chip)

        # Check that the Tensix reset worked by checking the scratch bit is cleared
        scratch_get = arc_chip.noc_read32(
            noc_id=0, x=1, y=2, addr=ETH_RISC_PREFECTH_CTRL_ADDR
        )
        assert scratch_get & 1 == 0, (
            "Tensix scratch bit not cleared. Tensix reset failed"
        )

        logger.info(f"Tensix reset test iteration {i} passed")


def test_aiclk(arc_chip_dut, asic_id):
    arc_chip = pyluwen.detect_chips()[asic_id]
    TARGET_AICLKS = [
        1350,  # Max value we rise to with GO_BUSY
        800,  # Value we settle to with GO_IDLE_LONG
        200,  # Minimum AICLK
    ]

    for clk in TARGET_AICLKS:
        arc_chip.arc_msg(ARC_MSG_TYPE_FORCE_AICLK, True, False, clk, 0, 1000)
        # Delay to allow AICLK to settle
        time.sleep(0.1)
        aiclk = arc_chip.arc_msg(ARC_MSG_TYPE_GET_AICLK)[0]
        assert aiclk == clk, f"Failed to set clock to {clk} MHz"
        logger.info(f"AICLK set to {aiclk} MHz successfully")


def test_mcuboot(unlaunched_dut, asic_id):
    """
    Validates that the SMC falls back to the recovery image
    when the main image is not valid.
    """
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
