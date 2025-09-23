#!/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0

"""
This script prepares a "recovery bundle". This bundle format is
specific to this scripting and does not match that of a firmware bundle.
Recovery bundles can be used with the "recover-blackhole" script to flash
a known working firmware onto a blackhole card.

The intention is that the recovery bundle is fully self contained,
and does not need this repository to be present to use it.
The bundle contains:
- recovery firmware for all cards we support
- pyocd flash algorithms needed to program the cards
- python protobuf files needed to patch the board ID section of the
  read only area of the EEPROM with a new serial number
- a YAML file describing the board names and recovery configurations
"""

import argparse
from pathlib import Path
import subprocess
import yaml
import tarfile
import tempfile
import os
import shutil
import sys
from intelhex import IntelHex

try:
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader

TT_Z_P_ROOT = Path(__file__).parents[3]
sys.path.append(str(TT_Z_P_ROOT / "scripts"))
# Local imports
import tt_boot_fs  # noqa: E402

with open(Path(__file__).parents[2] / "board_metadata.yaml") as f:
    BOARD_ID_MAP = yaml.load(f.read(), Loader=SafeLoader)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Utility to create recovery firmware binaries", allow_abbrev=False
    )
    parser.add_argument(
        "bundle_out",
        help="Output filename for recovery bundle",
    )
    return parser.parse_args()


def generate_recovery_assets(boardname, board_data, outdir):
    """
    Generates recovery assets for a given board, and writes them to
    the output directory
    @param boardname Board name to use in build targets
    @param board_data Dictionary of board data from board_metadata.yaml
    @param outdir Output directory to write recovery assets to
    """
    # First build the DMC using sysbuild, so we get the MCUBoot binary
    os.mkdir(outdir)
    with tempfile.TemporaryDirectory() as temp_dir:
        app_dir = TT_Z_P_ROOT / "app"
        dmc_build_dir = Path(temp_dir) / "dmc_build"
        bootfs_hex = Path(temp_dir) / "tt_boot_fs.hex"
        cmd = [
            "west",
            "build",
            "-p",
            "-d",
            str(dmc_build_dir),
            "-b",
            f"tt_blackhole@{boardname}/tt_blackhole/dmc",
            "--sysbuild",
            str(app_dir / "dmc"),
        ]
        print(f"Building for DMC on {boardname}: {' '.join(cmd)}")
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        smc_build_dir = Path(temp_dir) / "smc_build"
        cmd = [
            "west",
            "build",
            "-p",
            "-d",
            str(smc_build_dir),
            "-b",
            f"tt_blackhole@{boardname}/tt_blackhole/smc",
            "--sysbuild",
            str(app_dir / "smc"),
        ]
        print(f"Building for SMC on {boardname}: {' '.join(cmd)}")
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        env = {"$ROOT": str(TT_Z_P_ROOT), "$BUILD_DIR": str(smc_build_dir)}

        # Now, generate bootfs hex images for every ASIC on the board
        for asic in board_data:
            bootfs = tt_boot_fs.FileImage.load(
                smc_build_dir / f"{asic['bootfs-name']}.yaml", env
            ).to_boot_fs()
            # Extract the DMFW from the bootfs. We will use this DMFW in recovery, so that
            # the DMFW loaded into the DMC does not attempt to update itself to the copy in EEPROM
            with open(bootfs_hex, "wb") as f:
                f.write(bootfs.to_intel_hex())
            # Now build the full hex image.
            ih = IntelHex()
            # At 0x0, place the tt-boot-fs. This will be written to eeprom.
            ih.loadfile(bootfs_hex, format="hex")
            if "slot1_partition" in bootfs.entries:
                dmfw = bootfs.entries["slot1_partition"].data
                # Place mcuboot at 0x800_0000, which is the start of DMC flash
                ih.loadfile(
                    dmc_build_dir / "mcuboot" / "zephyr" / "zephyr.hex", format="hex"
                )
                # Place DMFW at 0x800_c000, which is the start of slot0
                ih.frombytes(dmfw, 0x800C000)
                # Write file out to 'recovery.hex'
            with open(outdir / f"{asic['bootfs-name']}_recovery.hex", "w") as f:
                ih.write_hex_file(f)
        # Now, we need to copy the protobuf files in. These assets are needed
        # To patch the read only board ID section with a new serial number
        proto_path = smc_build_dir / "smc" / "zephyr" / "python_proto_files"
        shutil.copytree(proto_path, outdir / "python_proto_files")


def main():
    args = parse_args()
    with tempfile.TemporaryDirectory() as temp_dir:
        for board in BOARD_ID_MAP:
            generate_recovery_assets(board, BOARD_ID_MAP[board], Path(temp_dir) / board)
        # Build the pyocd flash algorithms, so we can include them in the bundle
        cmd = [
            "cmake",
            f"-B{temp_dir}/build-bh-flm",
            "-S",
            str(
                TT_Z_P_ROOT
                / "scripts"
                / "tooling"
                / "blackhole_recovery"
                / "data"
                / "bh_flm"
            ),
        ]
        print(f"Building flash algorithms: {' '.join(cmd)}")
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        cmd = ["make", "-C", f"{temp_dir}/build-bh-flm", "-j", str(os.cpu_count())]
        print(f"Building flash algorithms: {' '.join(cmd)}")
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        os.mkdir(Path(temp_dir) / "build")
        # Array of files to copy
        file_ops = [
            (
                Path(temp_dir) / "build-bh-flm" / "spi1.flm",
                Path(temp_dir) / "build" / "spi1.flm",
            ),
            (
                Path(temp_dir) / "build-bh-flm" / "spi_combo.flm",
                Path(temp_dir) / "build" / "spi_combo.flm",
            ),
            (
                TT_Z_P_ROOT
                / "scripts"
                / "tooling"
                / "blackhole_recovery"
                / "data"
                / "bh_flm"
                / "pyocd_config_spi1.py",
                Path(temp_dir) / "pyocd_config_spi1.py",
            ),
            (
                TT_Z_P_ROOT
                / "scripts"
                / "tooling"
                / "blackhole_recovery"
                / "data"
                / "bh_flm"
                / "pyocd_config_spi_combo.py",
                Path(temp_dir) / "pyocd_config_spi_combo.py",
            ),
            (
                TT_Z_P_ROOT
                / "scripts"
                / "tooling"
                / "blackhole_recovery"
                / "data"
                / "bh_flm"
                / "pyocd_shared.py",
                Path(temp_dir) / "pyocd_shared.py",
            ),
            (
                TT_Z_P_ROOT / "scripts" / "board_metadata.yaml",
                Path(temp_dir) / "board_metadata.yaml",
            ),
        ]
        for src, dst in file_ops:
            shutil.copy(src, dst)
        # Drop the git revision into a text file
        git_rev = subprocess.run(
            ["git", "describe"], capture_output=True, universal_newlines=True
        )
        with open(Path(temp_dir) / "git_revision.txt", "w") as f:
            f.write(git_rev.stdout)
        # Now tar up the whole directory
        with tarfile.open(args.bundle_out, "w:gz") as tar:
            tar.add(temp_dir, arcname=".")
        print(f"Wrote recovery bundle to {args.bundle_out}")


if __name__ == "__main__":
    main()
