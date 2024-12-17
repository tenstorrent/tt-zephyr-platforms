#!/bin/bash

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

# Pre push hook to run compliance checks on a branch. Saves developers from
# waiting on CI to complete

set -e

# Read values git passes from stdin and discard them
while read local_ref local_oid remote_ref remote_oid; do
	:
done

echo "## Running compliance checks on commits. To skip these checks, run git push --no-verify ##"

if [ -z "${ZEPHYR_BASE}" ]; then
	zep_base=$(west list -f "{abspath}" zephyr)
	echo "ZEPHYR_BASE not set, using $zep_base"
else
	zep_base="${ZEPHYR_BASE}"
fi

manifest_base=$(west list -f "{abspath}" manifest)

# Exclude Kconfig check, as it only works on base Zephyr repo.
# ClangFormat and Ruff are excluded as they will return failures, but CI
# treats these as formatting hints only
$zep_base/scripts/ci/check_compliance.py \
	-e Kconfig \
	-e ClangFormat \
	-e Ruff \
	-n -o /dev/null \
	-c main..$HEAD

$manifest_base/scripts/check-copyright.py
