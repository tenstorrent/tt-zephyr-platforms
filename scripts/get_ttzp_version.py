#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

TTZP = Path(__file__).parent.parent


def get_ttzp_version(version_file: Path):
    with open(version_file, "r") as f:
        lines = [line.split("=")[1].strip() for line in f.readlines() if "=" in line]
    ver = ".".join(lines[:3])
    if lines[4]:
        ver += f"-{lines[4]}"
    return ver


def get_ttzp_version_u32(version_file: Path) -> int:
    """
    Parse VERSION file and return a u32 version number.
    Format: (major << 24) | (minor << 16) | (patchlevel << 8) | tweak
    """
    version_parts = {}

    with open(version_file, "r") as f:
        for line in f:
            if "=" in line:
                key, value = line.strip().split("=", 1)
                key = key.strip()
                value = value.strip()
                version_parts[key] = value

    # Extract version components
    major = int(version_parts.get("VERSION_MAJOR", 0))
    minor = int(version_parts.get("VERSION_MINOR", 0))
    patchlevel = int(version_parts.get("PATCHLEVEL", 0))
    tweak = int(version_parts.get("VERSION_TWEAK", 0))

    # Ensure values fit in their respective bit ranges
    if not 0 <= major <= 255:
        raise ValueError(f"VERSION_MAJOR {major} must be 0-255")
    if not 0 <= minor <= 255:
        raise ValueError(f"VERSION_MINOR {minor} must be 0-255")
    if not 0 <= patchlevel <= 255:
        raise ValueError(f"PATCHLEVEL {patchlevel} must be 0-255")
    if not 0 <= tweak <= 255:
        raise ValueError(f"VERSION_TWEAK {tweak} must be 0-255")

    # Pack into u32: major.minor.patchlevel.tweak
    version_u32 = (major << 24) | (minor << 16) | (patchlevel << 8) | tweak

    return version_u32


if __name__ == "__main__":
    print(get_ttzp_version(TTZP / "VERSION"))
