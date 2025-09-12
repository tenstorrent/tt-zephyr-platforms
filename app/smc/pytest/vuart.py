# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import os
import pytest
import subprocess

from pathlib import Path

from e2e_smoke import ARC_STATUS, get_arc_chip
from twister_harness import DeviceAdapter

SCRIPT_ROOT = Path(__file__)
MODULE_ROOT = SCRIPT_ROOT.parents[3]
SCRIPT_DIR = MODULE_ROOT / "scripts" / "tooling"


@pytest.fixture(scope="session")
def tt_console(tmp_path_factory: Path):
    tt_console_exe = tmp_path_factory.getbasetemp() / "tt-console"
    cmd = "make tt-console"
    subprocess.run(cmd.split(), capture_output=True, check=True, cwd=SCRIPT_DIR)
    cmd = f"cp {SCRIPT_DIR / 'tt-console'} {tt_console_exe}"
    subprocess.run(cmd.split(), capture_output=True, check=True)
    return tt_console_exe


@pytest.fixture(scope="session")
def arc_chip(unlaunched_dut: DeviceAdapter, asic_id):
    return get_arc_chip(unlaunched_dut, asic_id)


def test_compile_tt_console(tt_console: Path):
    assert os.access(tt_console, os.X_OK)


def test_boot_banner(tt_console: Path, arc_chip, asic_id):
    arc_chip.axi_read32(ARC_STATUS)

    # run 'tt-console' in quiet mode, and timeout after 3 s
    cmd = f"{tt_console} -d /dev/tenstorrent/{asic_id} -q -w 3000"
    proc = subprocess.run(cmd.split(), capture_output=True, check=True)

    out = proc.stdout.decode("utf-8")

    # check that the boot banner is seen on the console
    assert "Booting tt_blackhole with Zephyr OS" in out

    print(f"\nconsole output successfully captured:\n{out}")
