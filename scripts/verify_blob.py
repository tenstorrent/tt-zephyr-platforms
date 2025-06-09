# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import yaml
import hashlib
import os
import sys

# relative path from the root directory tt-zephyr_platforms to module.yml and blobs directory
MODULE_YAML_PATH = "zephyr/module.yml"
BLOBS_BASE_DIR = "zephyr/blobs/"


def get_cur_blobs():
    # Get the blobs defined in the module.yml file return as list of dicts
    if not os.path.exists(MODULE_YAML_PATH):
        print(f"Module YAML file {MODULE_YAML_PATH} does not exist.")
        return []
    with open(MODULE_YAML_PATH, "r") as file:
        module_data = yaml.safe_load(file)
    blobs = module_data.get("blobs", [])
    return blobs


def generate_expected_blob_sha256(blob):
    # Generate new/expected blob SHA256 checksums for the given blob file path
    blob_path = os.path.join(BLOBS_BASE_DIR, blob["path"])
    with open(blob_path, "rb") as file:
        content = file.read()
        expected_blob_sha256 = hashlib.sha256(content).hexdigest()
    return expected_blob_sha256


def verify_blobs():
    # Compare existing and most updated blob SHA256 checksums to see if they match
    cur_blobs = get_cur_blobs()
    if not cur_blobs:
        print("No blobs found in module.yml.")
        return 0

    for blob in cur_blobs:
        if not os.path.exists(os.path.join(BLOBS_BASE_DIR, blob["path"])):
            # blob file no longer exists, skip
            print(f"Blob file {blob['path']} does not exist.")
            continue

        cur_blob_sha256 = blob["sha256"]
        expected_blob_sha256 = generate_expected_blob_sha256(blob)
        if cur_blob_sha256 != expected_blob_sha256:
            print(
                f"Blob {blob['path']} has changed! Expected SHA256: {expected_blob_sha256}, but got {cur_blob_sha256}."
            )
            return 1

    print("All blobs SHA256sum verified successfully.")
    return 0


def main():
    return verify_blobs()


if __name__ == "__main__":
    sys.exit(main())
