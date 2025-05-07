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
CONSOLE_C = MODULE_ROOT / "scripts" / "tt-console" / "console.c"


@pytest.fixture(scope="session")
def tt_console(tmp_path_factory: Path):
    tt_console_inst_dir = tmp_path_factory.getbasetemp() / "tt-console-install"
    tt_console_exe = tt_console_inst_dir / "usr" / "local" / "bin" / "tt-console"
    cmd = f"./autogen.sh; DESTDIR={tt_console_inst_dir} ./configure;"
    cmd += f"DESTDIR={tt_console_inst_dir} make -j $(nproc);"
    cmd += f"DESTDIR={tt_console_inst_dir} make install"
    subprocess.run(
        cmd, capture_output=True, shell=True, check=True, cwd=CONSOLE_C.parents[0]
    )
    return tt_console_exe


@pytest.fixture(scope="session")
def arc_chip(unlaunched_dut: DeviceAdapter):
    return get_arc_chip(unlaunched_dut)


def test_compile_tt_console(tt_console: Path):
    assert os.access(tt_console, os.X_OK)


def test_boot_banner(tt_console: Path, arc_chip):
    arc_chip.axi_read32(ARC_STATUS)

    # run 'tt-console' in quiet mode, and timeout after 500ms
    cmd = f"{tt_console} -q -w 500"
    proc = subprocess.run(cmd.split(), capture_output=True, check=True)

    out = proc.stdout.decode("utf-8")

    # check that the boot banner is seen on the console
    assert "Booting tt_blackhole with Zephyr OS" in out

    print(f"\nconsole output successfully captured:\n{out}")
