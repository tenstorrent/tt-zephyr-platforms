#!/usr/bin/env python3
#
# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0
"""
Discover tests with build-ci tag from sample.yaml file.
Outputs JSON array of test names to stdout.
"""

import yaml
import json
import sys
import os


def discover_tests(sample_yaml_path):
    """Parse sample.yaml and find tests with build-ci tag."""
    try:
        with open(sample_yaml_path, "r") as f:
            data = yaml.safe_load(f)
    except FileNotFoundError:
        print(f"Error: {sample_yaml_path} not found", file=sys.stderr)
        sys.exit(1)
    except yaml.YAMLError as e:
        print(f"Error parsing YAML: {e}", file=sys.stderr)
        sys.exit(1)

    tests = []
    for test_name, test_config in data.get("tests", {}).items():
        tags = test_config.get("tags", [])

        # Handle both string and list tags
        if isinstance(tags, str):
            tags = [tags]

        if "build-ci" in tags:
            tests.append(test_name)

    return tests


def main():
    # Default to app/smc/sample.yaml relative to script location
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(script_dir)
    default_path = os.path.join(repo_root, "app", "smc", "sample.yaml")

    sample_yaml_path = sys.argv[1] if len(sys.argv) > 1 else default_path

    tests = discover_tests(sample_yaml_path)
    print(json.dumps(tests))


if __name__ == "__main__":
    main()
