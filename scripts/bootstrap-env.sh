#!/bin/bash

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

# This script sets up a user copy of tt-zephyr-platforms for development.
# It assumes that the Zephyr SDK and a common Python virtual environment
# are already installed at the machine level.

# Set error handling
set -e

# Function to check if Zephyr SDK is installed
check_zephyr_sdk() {
  if ls /opt/zephyr/zephyr-sdk* 1> /dev/null 2>&1; then
    echo "Zephyr SDK is already installed."
  else
    echo "Zephyr SDK is not installed."
    echo "Please follow the Zephyr Lab Machine Setup guide to install it."
    echo "https://tenstorrent.atlassian.net/wiki/spaces/syseng/pages/396919766/Zephyr+Lab+Machine+Setup"
    exit 1
  fi
}

# Function to activate the common Python virtual environment
activate_python_env() {
  source /opt/zephyr/.venv/bin/activate
  if [ $? -ne 0 ]; then
    echo "Failed to activate the Python virtual environment."
    exit 1
  fi
}

# Function to clone tt-zephyr-platforms repository
clone_repository() {
  mkdir -p ~/tt-zephyr
  cd ~/tt-zephyr
  git clone git@github.com:tenstorrent/tt-zephyr-platforms.git tt-zephyr-platforms
  cd tt-zephyr-platforms
  west init -l
}

# Function to pull manifest revisions needed to build DMFW and CMFW
pull_manifest_revisions() {
  west config manifest.group-filter +optional
  west config build.dir-fmt "build/{board}/{app}"
  west update
}

# Function to apply patches to Zephyr
apply_patches() {
  west patch apply
}

# Function to build firmware
build_firmware() {
  # Builds for SMC
  west build -p always -b tt_blackhole/tt_blackhole/smc app/smc/

  # Builds for DMC
  west build -p always -b tt_blackhole@p100/tt_blackhole/dmc app/dmc/
}

# Main script execution
main() {
  # Check if Zephyr SDK is installed
  check_zephyr_sdk

  # Go to home directory
  cd ~

  # Activate the common Python virtual environment
  activate_python_env

  # Clone the tt-zephyr-platforms repository
  clone_repository

  # Pull manifest revisions needed to build DMFW and CMFW
  pull_manifest_revisions

  # Apply patches to Zephyr
  apply_patches

  # Build firmware for SMC and DMC to test the environment
  build_firmware

  echo "Setup and build completed successfully."
}

# Run the main function
main
