#!/bin/bash

# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0

# GitHub Actions send SIGTERM to the runner process on job cancellation. If the process does not
# exit within TimeoutStopSec (which is 5 minutes, by default), it will be sent SIGKILL. This can
# be inferred by examining the GitHub Actions systemd unit.
#
# $ grep "SIG\|Timeout" /etc/systemd/system/actions.runner*.service
# KillSignal=SIGTERM
# TimeoutStopSec=5min
#
# It is impossible to catch SIGKILL (signal 9). However, we can catch SIGTERM (signal 15).
#
# If we do not catch and ignore this signal, then the recovery process may be interrupted leaving
# the device in an unrecovered state. Recovery must complete within 5 minutes after receiving
# SIGTERM. Currently, it only takes a few minutes to recover.
SIGTERM_handler() {
    echo "Ignoring caught SIGTERM to ensure smooth recovery..."
}
trap SIGTERM_handler SIGTERM

IMAGE_URL="ghcr.io/tenstorrent/tt-zephyr-platforms/recovery-image"
IMAGE_TAG=${IMAGE_TAG:-"v18.12.0-rc1"}
if [[ -n $BOARD_SERIAL ]]; then
    SERIAL_ARG="--board-id ${BOARD_SERIAL}"
fi
if [[ -n $BOARD_NAME ]]; then
    NAME_ARG="${BOARD_NAME}"
fi
if [[ $GITHUB_RUN_ATTEMPT -gt 1 ]]; then
    # Set within github runner environment for retries, if not first attempt
    # let's force recovery
    FORCE_ARG="--force"
fi

# This script will download and run the blackhole recovery tool inside a
# Docker container. Note that a recovery bundle can also be
# built and flashed manually, see the README.md for details.
if ! command -v docker >/dev/null 2>&1; then
    echo "Docker not installed. Please install Docker to proceed."
    exit 1
fi

if [[ -z $BOARD_NAME && $# -lt 1 ]]; then
    echo "Usage: $0 <board name>"
    exit 1
fi

if ! docker info >/dev/null 2>&1; then
    echo "You do not have permission to run Docker commands."\
        "Please ensure your user is in the 'docker' group or run as root."
    exit 1
fi

# Pull the latest image
docker pull $IMAGE_URL:$IMAGE_TAG

echo "Launching docker container to recover blackhole device..."
docker run --device /dev/bus/usb --privileged \
    --rm $IMAGE_URL:$IMAGE_TAG \
    python3 /tt-zephyr-platforms/scripts/tooling/blackhole_recovery/recover-blackhole.py \
    /recovery.tar.gz $NAME_ARG $SERIAL_ARG $FORCE_ARG $@
