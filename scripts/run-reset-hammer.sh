#!/bin/bash

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

BOARD=$1
COUNT=$2

if [ $# -ne 2 ]; then
	echo "Incorrect number of arguments, expected board name and test count"
	exit 1
fi

# Repeatedly resets the SMC. Intended to be run within CI.
# Accepts two arguments:
# 1. board name to use
# 2. the number of times to run the reset test

TT_Z_P="$(dirname $(realpath $0))/.."
echo $TT_Z_P

fail_cnt=0
pass_cnt=0

function report_results()
{
	echo "Reset test ended: $pass_cnt passed, $fail_cnt failed"

	if [ $fail_cnt -ne 0 ]; then
		echo "Reset test failed with $fail_cnt errors"
		exit 1
	else
		echo "Reset test passed successfully"
		exit 0
	fi
}

trap report_results SIGINT

# Build the test
west build -p always -b $BOARD -d build-reset-test $TT_Z_P/app/smc

for i in $(seq 1 $COUNT); do
	# Flash twice, this gives us the best chance of resetting the SMC while
	# I2C init is ongoing
	echo "Flashing SMC twice"
	west flash -d build-reset-test > /dev/null 2>&1
	west flash -d build-reset-test > /dev/null 2>&1
	# Rescan PCI
	sudo $TT_Z_P/scripts/rescan-pcie.sh
	# Test with tt-smi
	tt-smi -s > /dev/null
	if [ $? -ne 0 ]; then
		echo "Reset test failed on iteration $i"
		((fail_cnt++))
	else
		echo "Reset test passed on iteration $i"
		((pass_cnt++))
	fi
done

# Call exit function on script completion
report_results
