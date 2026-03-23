#!/bin/bash

# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Tenstorrent AI ULC

# This script runs grendel smoke tests

set -e

# Pull required repos
# Clone the VDK utilities repo, which contains a grendel fast functional SIM
git clone git@yyz-gitlab.local.tenstorrent.com:syseng-platform/vdk-utils.git
# Pull tt-smc repo to run Zephyr tests
git clone git@yyz-gitlab.local.tenstorrent.com:tensix/tensix-hw/tt_smc.git

cd vdk-utils
TWISTER_DIR=$OUTDIR/tt_mimir_tt_mimir_smc/zephyr
ZEPHYR_ELF=$TWISTER_DIR/tt-system-firmware/tests/drivers/tt_smc_remoteproc/
ZEPHYR_ELF+=drivers.tt_smc_remoteproc.bl1_primary/tt_smc_remoteproc/zephyr/zephyr.elf
PROD_ROM_ELF=../tt_smc/firmware/prod_rom-1.1.1-20260117-794e39bc/build/release/bin/prod_rom.elf
# Watch this command until it outputs "Test PASSED"
mkdir ../vdk-logs
timeout 300 bash -c '
    ./run-smc-headless.sh "$1" "$2" | while read -r line; do
        echo "$line" >> ../vdk-logs/grendel-remoteproc-smoke.log
        if echo "$line" | grep -q "Test PASSED"; then
            echo "Found target output, exiting..."
            exit 0
        fi
    done
' _ "$ZEPHYR_ELF" "$PROD_ROM_ELF"
# Back out of tt-smc repo
cd ..
cd tt_smc
source bin/setup_env.sh
module list || true
bender checkout --force || bender checkout --force
# Patch ttem command to extend test timeout
sed -i "s/run_args: +COCOTB_TEST=smc_zephyr_binary_loader_test"\
" +FW_TEST=grendel_smc_hello_world_smp_zephyr/run_args:"\
" +COCOTB_TEST=smc_zephyr_binary_loader_test"\
" +FW_TEST=grendel_smc_hello_world_smp_zephyr"\
" +FW_TEST_TIMEOUT=1000000000/g" tb_uvm/yaml/regression_smc_chiplet.yaml
cp "$TWISTER_DIR/tests/drivers/uart/"\
"uart_elementary/drivers.uart.uart_elementary.grendel_uart/zephyr/zephyr.bin" \
   ./firmware/zephyr/grendel_smc_hello_world_smp_zephyr/grendel_smc_hello_world_smp_zephyr.bin
ttem tb_uvm/yaml/regression_smc_chiplet.yaml smc_zephyr_hello_world_smp_test \
	--stack flist,cgen,compile_smc_chiplet,sim --no-wave --lsf --seed 1 --c compile_smc_chiplet
