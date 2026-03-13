#!/bin/bash

# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Tenstorrent AI ULC

# This script runs ttem tests using the ttem repository

set -e

# Patch ttem command to extend test timeout
sed -i "s/run_args: +COCOTB_TEST=smc_zephyr_binary_loader_test"\
" +FW_TEST=grendel_smc_hello_world_smp_zephyr/run_args:"\
" +COCOTB_TEST=smc_zephyr_binary_loader_test"\
" +FW_TEST=grendel_smc_hello_world_smp_zephyr"\
" +FW_TEST_TIMEOUT=2000000/g" tb_uvm/yaml/regression_smc_chiplet.yaml
cp "$OUTDIR/tt_grendel_smc_tt_grendel_smc/zephyr/tests/drivers/uart/"\
"uart_elementary/drivers.uart.uart_elementary.grendel_uart/zephyr/zephyr.bin" \
   ./firmware/zephyr/grendel_smc_hello_world_smp_zephyr/grendel_smc_hello_world_smp_zephyr.bin
ttem tb_uvm/yaml/regression_smc_chiplet.yaml smc_zephyr_hello_world_smp_test \
	--stack flist,cgen,compile_smc_chiplet,sim --no-wave --lsf --seed 1 --c compile_smc_chiplet
