#!/bin/bash

# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Tenstorrent AI ULC

# This script builds grendel platforms within the tt-zephyr-platforms CI
# image environment.

OUTDIR=${OUTDIR:-twister-out}

# Build all grendel platforms
./zephyr/scripts/twister -p tt_grendel_smc -T ./zephyr/samples/hello_world --outdir $OUTDIR
