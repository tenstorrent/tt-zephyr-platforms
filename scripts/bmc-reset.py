#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import argparse
import os
import subprocess
import sys
import yaml
import logging

from pathlib import Path

logger = logging.getLogger(Path(__file__).stem)

OPT_DIR = Path("/opt/tenstorrent")
SDK_SYSROOT = Path("/opt/zephyr/zephyr-sdk-0.17.0/sysroots/x86_64-pokysdk-linux")


def parse_args():
    parser = argparse.ArgumentParser(description="Reset BMC", allow_abbrev=False)
    parser.add_argument(
        "-c",
        "--config",
        default=OPT_DIR
        / "fw"
        / "stable"
        / "tt_blackhole_tt_blackhole_bmc"
        / "tt_blackhole_bmc.cfg",
        help="OpenOCD config file",
        metavar="FILE",
        type=Path,
    )
    parser.add_argument(
        "-d",
        "--debug",
        action="count",
        default=0,
        help="Increase debugging verbosity",
    )
    parser.add_argument(
        "-i",
        "--jtag-id",
        default="auto",
        help="Specify the JTAG ID / serial number for the debug adapter",
        metavar="ID",
    )
    parser.add_argument(
        "-m",
        "--hw-map",
        default=OPT_DIR / "twister" / "hw-map.yml",
        help="Use a specific hw-map.yml file",
        metavar="MAP",
        type=Path,
    )
    parser.add_argument(
        "-o",
        "--openocd",
        default=SDK_SYSROOT / "usr" / "bin" / "openocd",
        help="Use a specific hw-map.yml file",
        metavar="FILE",
        type=Path,
    )
    parser.add_argument(
        "-s",
        "--scripts",
        default=SDK_SYSROOT / "usr" / "share" / "openocd" / "scripts",
        help="Path to OpenOCD scripts directory",
        metavar="DIR",
        type=Path,
    )
    parser.add_argument(
        "hexfile",
        help="Hex file to flash, if needed",
        metavar="HEX",
        nargs="?",
        type=Path,
    )

    args = parser.parse_args()

    if not args.config.is_file():
        parser.error(f"Config file {args.config} does not exist")
        return None

    if not args.hw_map.is_file():
        parser.error(f"Hardware map file {args.hw_map} does not exist")
        return None

    if not (args.openocd.is_file() and os.access(str(args.openocd), os.X_OK)):
        parser.error(f"{args.openocd} is not an executable file")
        return None

    if not args.scripts.is_dir():
        parser.error(f"{args.scripts} is not a directory")
        return None

    return args


def reset_bmc(args):
    openocd_cmd = [
        str(args.openocd),
        "-s",
        str(args.scripts),
        "-f",
        str(args.config),
        "-c",
        f"adapter serial {args.jtag_id}",
    ]

    if args.hexfile:
        # program the hex file and reset the BMC
        openocd_cmd.extend(
            [
                "-c",
                "init",
                "-c",
                "targets",
                "-c",
                "reset init",
                "-c",
                f"flash write_image erase {args.hexfile}",
                "-c",
                "reset run; exit",
            ]
        )
    else:
        # simply reset the BMC
        openocd_cmd.extend(["-c", "init", "-c", "targets", "-c", "reset run; exit"])

    if args.debug > 0:
        openocd_kwargs = {
            "stdout": subprocess.PIPE,
            "stderr": subprocess.PIPE,
        }
    else:
        openocd_kwargs = {
            "stdout": subprocess.DEVNULL,
            "stderr": subprocess.DEVNULL,
        }

    logger.debug("running command: " + " ".join(openocd_cmd))

    proc = subprocess.run(openocd_cmd, **openocd_kwargs)
    if proc.returncode != 0:
        logger.error(f"Failed to reset BMC: {proc.stderr}")
        return os.EX_SOFTWARE

    return os.EX_OK


def main():
    args = parse_args()
    if args is None:
        return os.EX_DATAERR

    if args.debug > 0:
        logging.basicConfig(level=logging.DEBUG)

    if args.jtag_id == "auto":
        logger.debug("Auto-detecting JTAG ID..")

        try:
            hwmap = yaml.load(open(args.hw_map, "r"), Loader=yaml.Loader)
        except Exception as e:
            logger.error(f"Failed to load hardware map: {e}")
            return os.EX_DATAERR

        for dut in hwmap:
            if not dut["connected"]:
                continue
            if dut["platform"].endswith("bmc"):
                setattr(args, "jtag_id", dut["id"])
                logger.debug(f"Found {dut['platform']} with JTAG ID {dut['id']}")
                break

    return reset_bmc(args)


if __name__ == "__main__":
    sys.exit(main())
