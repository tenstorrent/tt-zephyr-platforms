# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

from e2e_smoke import get_arc_chip
from twister_harness import DeviceAdapter
import pytest


@pytest.fixture(scope="session")
def arc_chip_dut(unlaunched_dut: DeviceAdapter, asic_id):
    get_arc_chip(unlaunched_dut, asic_id)
    return unlaunched_dut


def test_boot_banner(arc_chip_dut):
    """
    Validates that the boot banner is printed correctly. This should run before
    any tests that reset the chip.
    """
    # Wait 3 seconds for console output
    output = arc_chip_dut.readlines_until(
        "Booting tt_blackhole with Zephyr OS", timeout=3000
    )
    out_str = "\n".join(output)
    assert "Booting tt_blackhole with Zephyr OS" in out_str
    print(f"\nconsole output successfully captured:\n{out_str}")
