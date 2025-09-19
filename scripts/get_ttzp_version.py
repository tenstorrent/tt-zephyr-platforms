#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

TTZP = Path(__file__).parent.parent


def get_ttzp_version():
    with open(TTZP / "VERSION", "r") as f:
        lines = [line.split("=")[1].strip() for line in f.readlines() if "=" in line]
    ver = ".".join(lines[:3])
    if lines[4]:
        ver += f"-{lines[4]}"
    return ver


if __name__ == "__main__":
    print(get_ttzp_version())
