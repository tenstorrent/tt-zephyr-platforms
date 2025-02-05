#!/usr/bin/env python3
#
# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import glob
import os
import re
import sys

# Only check the first N lines of each file
LINE_LIMIT = 25
# Any copyright (case-insensitive)
ANY_COPYRIGHT_PATTERN = re.compile(r".*copyright.*", re.IGNORECASE)
# General, case-insensitive pattern for a copyright to inspect
HAYSTACK_COPYRIGHT_PATTERN = re.compile(r".*copyright.*tenstorrent.*", re.IGNORECASE)
# Specific, case-sensitive pattern to check against
NEEDLE_COPYRIGHT_PATTERN = re.compile(
    r".*Copyright \(c\) 20[12][0-9] Tenstorrent AI ULC"
)

# Set of globs for file paths to check
PATHS = {
    # Assembly sources
    "**/*.asm",
    "**/*.s",
    "**/*.S",
    # C sources
    "**/*.c",
    "**/*.h",
    # C++ sources
    "**/*.cc",
    "**/*.cpp",
    "**/*.cxx",
    "**/*.hpp",
    # Devicetree bindings
    "dts/**/*.yaml",
    "dts/**/*.yml",
    # Devicetree sources
    "**/*.dts",
    "**/*.dtsi",
    # Kconfig files
    "**/Kconfig*",
    # Python sources
    "**/*.py",
    # Protobuf sources
    "**/*.proto",
    # Rust sources
    "**/*.rs",
    # Shell scripts
    "**/*.sh",
}

# Set of dirs to exclude from check
EXCLUDE_DIRS = {"build/**", "twister-out*/**"}


def is_excluded(filename):
    for pattern in EXCLUDE_DIRS:
        if glob.fnmatch.fnmatch(filename, pattern):
            return True
    return False


num_files = 0
error_count = 0
tt_copyrights = 0
other_copyrights = 0
no_copyrights = 0

for path in PATHS:
    for filename in glob.glob(path, recursive=True):
        num_files += 1
        if os.path.basename(filename) == os.path.basename(__file__):
            # at least on macOS, this script cannot read itself ¯\_(ツ)_/¯
            continue

        if is_excluded(filename):
            # don't check files in generated dirs
            continue

        with open(filename, "r") as f:
            found = False
            for line_no in range(LINE_LIMIT):
                line = f.readline()
                if line is None:
                    break

                line = line.strip()

                # FIXME: none of the checks below account for improper spelling

                if ANY_COPYRIGHT_PATTERN.match(line):
                    found = True
                    if HAYSTACK_COPYRIGHT_PATTERN.match(line):
                        tt_copyrights += 1
                        if not NEEDLE_COPYRIGHT_PATTERN.match(line):
                            print(
                                f"::error file={filename},line={line_no}::malformed copyright: '{line}'"
                            )
                            error_count += 1
                        break
                    else:
                        # display an warning in the "Code Review" area about 3rd-party copyrights
                        print(
                            f"::warning file={filename},line={line_no}::3rd party copyright found: '{line}'"
                        )
                        other_copyrights += 1

            if not found:
                print(f"::error file={filename},line=1::missing copyright")
                error_count += 1
                no_copyrights += 1

print(f"Checked {num_files} files")
print(f"{tt_copyrights} files are copyrighted by Tenstorrent")
print(f"{other_copyrights} files are at least partially copyrighted by others")
print(f"{no_copyrights} files have no copyright")
print(f"Found {error_count} copyright errors")

if error_count:
    print(f"Failure due to {error_count} copyright errors")
    sys.exit(1)

print("All files copyrighted by Tenstorrent have correct copyright format")
