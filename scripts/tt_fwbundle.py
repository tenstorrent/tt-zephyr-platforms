#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""
Tools to manage Tenstorrent firmware bundles.
Supports creating and combining firmware bundles,
as well as extracting their contents.
"""

import argparse
from intelhex import IntelHex
from base64 import b16encode
from pathlib import Path
import os
import sys
import shutil
import json
import tarfile
import tempfile


def combine_fw_bundles(combine: list[Path], output: Path):
    """
    Combines multiple firmware bundle files into a single tar.gz file.
    """
    # Create a temporary directory to extract bundles into
    temp_dir = Path(tempfile.mkdtemp())
    # Extract each bundle into the temp directory
    try:
        for bundle in combine:
            with tarfile.open(bundle, "r:gz") as tar:
                tar.extractall(path=temp_dir, filter="data")
        # Create combined bundle
        if output.exists():
            output.unlink()
        with tarfile.open(output, "x:gz") as tar:
            tar.add(temp_dir, arcname=".")

    except Exception as e:
        raise e
    finally:
        shutil.rmtree(temp_dir)


def create_fw_bundle(output: Path, version: list[int], boot_fs: dict[str, Path] = {}):
    """
    Creates a firmware bundle tar.gz file, from tt_boot_fs images.
    """
    bundle_dir = Path(tempfile.mkdtemp())
    try:
        # Process (board, tt_boot_fs) pairs
        for board, image in boot_fs.items():
            board_dir = bundle_dir / board
            board_dir.mkdir()
            mask = [{"tag": "write-boardcfg"}]
            with open(board_dir / "mask.json", "w") as file:
                file.write(json.dumps(mask))
            mapping = []
            with open(board_dir / "mapping.json", "w") as file:
                file.write(json.dumps(mapping))
            if image.suffix == ".hex":
                # Encode offsets using @addr format tt-flash supports
                ih = IntelHex(str(image))
                b16out = ""
                for off, end in ih.segments():
                    b16out += f"@{off}\n"
                    b16out += b16encode(
                        ih.tobinarray(start=off, size=end - off)
                    ).decode("ascii")
                    b16out += "\n"
            else:
                with open(image, "rb") as img:
                    binary = img.read()
                    # Convert image to base16 encoded ascii to conform to
                    # tt-flash format
                    b16out = b16encode(binary).decode("ascii")
            with open(board_dir / "image.bin", "w") as img:
                img.write(b16out)

        # Update the manifest last, so we can specify the bundle_version
        manifest = {
            "version": "2.0.0",  # manifest file version
            "bundle_version": {
                "fwId": version[0],
                "releaseId": version[1],
                "patch": version[2],
                "debug": version[3],
            },
        }
        with open(bundle_dir / "manifest.json", "w+") as file:
            file.write(json.dumps(manifest))

        # Compress output as tar.gz
        if output.exists():
            output.unlink()
        with tarfile.open(output, "x:gz") as tar:
            tar.add(bundle_dir, arcname=".")

    except Exception as e:
        raise e
    finally:
        shutil.rmtree(bundle_dir)


def invoke_create_fw_bundle(args):
    # Convert e.g. "80.16.0.1" to [80, 16, 0, 1]
    version = args.version.split(".")
    if len(version) != 4:
        raise RuntimeError("Invalid bundle version format")
    for i in range(4):
        version[i] = int(version[i])
    setattr(args, "version", version)

    # e.g. bootfs["P100-1"] = blah/p100/tt_boot_fs.bin
    if len(args.bootfs) % 2 != 0:
        raise RuntimeError(f"Invalid number of boot fs arguments: {len(args.boot_fs)}")
    bootfs = {}
    for i in range(0, len(args.bootfs), 2):
        bootfs[args.bootfs[i]] = Path(args.bootfs[i + 1])
    setattr(args, "bootfs", bootfs)

    create_fw_bundle(args.output, args.version, args.bootfs)
    print(f"Wrote fwbundle to {args.output}")
    return os.EX_OK


def invoke_combine_fw_bundle(args):
    combine_fw_bundles(args.bundles, args.output)
    print(f"Wrote combined fwbundle to {args.output}")
    return os.EX_OK


def parse_args():
    """
    Parse command line arguments.
    """
    parser = argparse.ArgumentParser(
        description="Tools to manage Tenstorrent firmware bundles.",
        allow_abbrev=False,
    )
    subparsers = parser.add_subparsers()

    # Create a firmware bundle
    fw_bundle_create_parser = subparsers.add_parser(
        "create", help="Create a firmware bundle"
    )
    fw_bundle_create_parser.set_defaults(func=invoke_create_fw_bundle)
    fw_bundle_create_parser.add_argument(
        "-v",
        "--version",
        metavar="VERSION",
        help="bundle version (e.g. 80.16.0.1)",
        required=True,
    )
    fw_bundle_create_parser.add_argument(
        "-o",
        "--output",
        metavar="BUNDLE",
        help="output bundle file name",
        type=Path,
        required=True,
    )
    fw_bundle_create_parser.add_argument(
        "bootfs",
        metavar="BOARD_FS",
        help="[PREFIX FS..] pairs (e.g. P150A-1 build-p150a/tt_boot_fs.bin)",
        nargs="*",
        default=[],
    )
    # Combine multiple firmware bundles
    fw_bundle_combine_parser = subparsers.add_parser(
        "combine", help="Combine multiple firmware bundles"
    )
    fw_bundle_combine_parser.set_defaults(func=invoke_combine_fw_bundle)
    fw_bundle_combine_parser.add_argument(
        "-o",
        "--output",
        metavar="BUNDLE",
        help="output bundle file name",
        type=Path,
        required=True,
    )
    fw_bundle_combine_parser.add_argument(
        "bundles",
        metavar="BUNDLES",
        help="input bundle files to combine",
        nargs="+",
        default=[],
    )

    args = parser.parse_args()
    if not hasattr(args, "func"):
        print("No command specified")
        parser.print_help()
        sys.exit(os.EX_USAGE)

    return args


def main():
    """
    Main entry point for tt_fwbundle script.
    """
    args = parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
