# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import sys
import argparse

from pathlib import Path
from typing import Any
from google.protobuf import text_format


def obj_to_inc(obj: Any, bundle_version: int) -> str:
    """Convert a fw_table_pb2.FwTable object to a C struct declaration.
    Note: the object argument is of type Any due to dynamic python imports.
    """
    s = ""
    for descriptor in obj.DESCRIPTOR.fields:
        if descriptor.name == "fw_bundle_version":
            value = bundle_version
        else:
            value = getattr(obj, descriptor.name)
        if descriptor.type == descriptor.TYPE_MESSAGE:
            s += f".{descriptor.name} = {{"
            if descriptor.label == descriptor.LABEL_REPEATED:
                s += map(obj_to_inc, value, bundle_version)
            else:
                s += obj_to_inc(value, bundle_version)
            s += "},"
        else:
            if isinstance(value, bool):
                value = "1" if value else "0"
            s += f".{descriptor.name} = {value},"

    return s


def convert_proto_txt_to_inc_file(input, output, bundle_version):
    """Convert a text representation of a FwTable to a C struct declaration and write it to the
    output file."""
    try:
        import fw_table_pb2
    except ImportError as e:
        print(f"Error importing protobuf modules: {e}")
        print("Ensure the protobuf files are generated and the path is correct.")
        sys.exit(1)

    fw_table_txt = ""

    # Read the text representation of the FwTable
    with open(input, "r") as f:
        fw_table_txt = f.read()

    # Parse the text representation into a FwTable object
    fw_table_obj = fw_table_pb2.FwTable()
    fw_table = text_format.Parse(fw_table_txt, fw_table_obj)

    # Convert the FwTable object to a C struct declaration
    inc = obj_to_inc(fw_table, bundle_version)

    # Write the C struct declaration to the output file
    output.parent.mkdir(parents=True, exist_ok=True)
    with open(output, "w") as f:
        f.write(inc)


def main():
    parser = argparse.ArgumentParser(
        description="Encode SPIROM bins", allow_abbrev=False
    )
    parser.add_argument(
        "-i",
        "--input",
        type=Path,
        help="Path to input fw_table.txt file",
        required=True,
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Path to output .inc file of FwTable content",
        required=True,
    )
    parser.add_argument(
        "-p",
        "--python-path",
        type=Path,
        action="append",
        default=[],
        metavar="PYTHON_PATH",
        help="Additional python path to search for fw_table_pb2. May be specified more than once",
    )
    parser.add_argument(
        "-v",
        "--bundle-version",
        type=lambda x: int(x, 0),
        help="Bundle version number expressed as an integer",
        required=True,
    )
    args = parser.parse_args()

    for p in args.python_path:
        if p not in sys.path:
            sys.path.append(str(p))

    convert_proto_txt_to_inc_file(args.input, args.output, args.bundle_version)


if __name__ == "__main__":
    main()
