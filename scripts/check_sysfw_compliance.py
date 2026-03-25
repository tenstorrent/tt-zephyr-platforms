#!/usr/bin/env python3
#
# Copyright (c) 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""
TT System Firmware Compliance Checker

This script imports and runs the Zephyr compliance checker for tt-system-firmware.
"""

import sys
from pathlib import Path

TT_SYS_FW_ROOT = Path(__file__).parent.absolute()
ZEPHYR_ROOT = TT_SYS_FW_ROOT.parents[1] / "zephyr"
zephyr_scripts_dir = ZEPHYR_ROOT / "scripts" / "ci"

sys.path.insert(0, str(zephyr_scripts_dir))
import check_compliance  # noqa: E402


# Patch all ComplianceTest inheritors to convert warnings to errors
def err_fmtd(original, always_error):
    return lambda self, severity, *args, **kwargs: original(
        self,
        "error" if severity == "warning" or always_error else severity,
        *args,
        **kwargs,
    )


# items in this list will return the severity based on zephyr upstream
default_fmtd = ["Checkpatch"]
# items in this list will always return a severity of error
all_errors = ["ClangFormat"]

# all other items will convert warnings to errors only
for cls in check_compliance.inheritors(check_compliance.ComplianceTest):
    if cls.name not in default_fmtd:
        cls.fmtd_failure = err_fmtd(cls.fmtd_failure, cls.name in all_errors)


if __name__ == "__main__":
    check_compliance.main()
