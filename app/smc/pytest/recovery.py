#!/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import logging
import pyluwen
import sys
import time
import yaml

import tt_boot_fs
import tt_fwbundle

from intelhex import IntelHex
from pathlib import Path
from pcie_utils import rescan_pcie
from e2e_smoke import DeviceAdapter

# Import tt_boot_fs utilities
sys.path.append(str(Path(__file__).parents[3] / "scripts"))

logger = logging.getLogger(__name__)

# Constant memory addresses we can read from SMC
ARC_POSTCODE_STATUS = 0x80030060
# Boot status register
ARC_BOOT_STATUS = 0x80030408


def _suffix_from_path(path: Path) -> str:
    stem = path.stem
    parts = stem.split("tt_boot_fs", 1)
    return parts[1] if len(parts) > 1 else ""


def read_boot_status(chip) -> int:
    """
    Helper to read the PCIe status register
    """
    try:
        status = chip.axi_read32(ARC_POSTCODE_STATUS)
    except Exception:
        print("Warning- SMC firmware requires a reset. Rescanning PCIe bus")
        rescan_pcie()
        status = chip.axi_read32(ARC_POSTCODE_STATUS)
    assert (status & 0xFFFF0000) == 0xC0DE0000, "SMC firmware postcode is invalid"
    # Check post code status of firmware
    assert (status & 0xFFFF) >= 0x1D, "SMC firmware boot failed"
    return chip.axi_read32(ARC_BOOT_STATUS)


def check_recovery_active(recovery: bool) -> bool:
    """
    Checks if the recovery firmware is active based on boot status register
    """
    chips = pyluwen.detect_chips()
    if len(chips) == 0:
        raise RuntimeError("PCIe card was not detected on this system")
    for chip in chips:
        if recovery:
            assert (read_boot_status(chip) & 0x78) == 0x8, (
                "Recovery firmware should be active"
            )
        else:
            assert (read_boot_status(chip) & 0x78) == 0x0, (
                "Recovery firmware should not be active"
            )


def test_recovery_cmfw(unlaunched_dut: DeviceAdapter):
    """
    Tests flashing a bad base CMFW, and makes sure the SMC boots the recovery
    CMFW. Verifies the recovery CMFW is running, and then flashes a working CMFW
    back to the card
    """
    # Get the build directory of the DUT
    build_dir = unlaunched_dut.device_config.build_dir

    board_fs_dict = {}
    # Iterate through tt_boot_fs files for various boards in the build dir
    for boot_fs in sorted(build_dir.glob("tt_boot_fs*.hex")):
        # Create path for patched tt_boot_fs
        suffix = _suffix_from_path(boot_fs)
        patched_fs = build_dir / f"tt_boot_fs{suffix}_patched.bin"

        assert boot_fs.exists(), f"{boot_fs.name} not found at {boot_fs}"
        bootfs_data = IntelHex(str(boot_fs)).tobinarray().tobytes()
        fs = tt_boot_fs.BootFs.from_binary(bootfs_data)

        # Write copy of tt_boot_fs to new file
        patched_fs.write_bytes(bootfs_data)

        # Corrupt offset of base CMFW (main image)
        smc_offset = fs.entries["mainimg"].spi_addr
        with open(patched_fs, "r+b") as f:
            f.seek(smc_offset)
            f.write(b"BAD DATA")
        logger.info(
            f"Corrupted data at offset {hex(smc_offset)} for tt_boot_fs{suffix}"
        )

        # Add damaged tt_boot_fs to dict
        yaml_suffix = suffix.replace("-", "_")
        boot_fs_yaml = build_dir / f"tt_boot_fs{yaml_suffix}.yaml"
        with open(str(boot_fs_yaml), "r") as file:
            board_name = yaml.safe_load(file)["name"]
        board_fs_dict[board_name] = patched_fs
    # Create a new fw bundle with the damaged CMFWs
    tt_fwbundle.create_fw_bundle(
        build_dir / "tt_boot_fs_patched.bundle",
        [99, 99, 99, 99],
        board_fs_dict,
    )

    # Flash the damaged CMFW
    unlaunched_dut.command = [
        unlaunched_dut.west,
        "flash",
        "--build-dir",
        str(build_dir),
        "--runner",
        "tt_flash",
        "--force",
        "--no-rebuild",
        "--",
        "--file",
        str(build_dir / "tt_boot_fs_patched.bundle"),
    ]
    unlaunched_dut._flash_and_run()
    time.sleep(1)
    check_recovery_active(True)

    # Flash the good CMFW back. Note- this requires an up to date version of tt-flash
    unlaunched_dut.command = [
        unlaunched_dut.west,
        "flash",
        "--build-dir",
        str(build_dir),
        "--runner",
        "tt_flash",
        "--force",
        "--no-rebuild",
    ]
    unlaunched_dut._flash_and_run()
    time.sleep(1)
    check_recovery_active(False)
