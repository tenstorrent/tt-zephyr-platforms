#!/bin/bash

# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Tenstorrent AI ULC

# This script builds grendel platforms within the tt-zephyr-platforms CI
# image environment.

OUTDIR=${OUTDIR:-twister-out}

# Build all grendel platforms
./zephyr/scripts/twister -p tt_mimir/tt_mimir/smc \
	-T ./zephyr/tests/ \
	-T ./zephyr/samples/ \
	--alt-config-root ./tt-system-firmware/test-conf/tests/ \
	--alt-config-root ./tt-system-firmware/test-conf/samples/ \
	-T ./tt-system-firmware/tests/ \
	-T ./tt-system-firmware/samples/ \
	--tag smoke \
	--outdir $OUTDIR
