#!/bin/env bash

# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Tenstorrent AI ULC

# This script runs the manufacturing test sequence for Blackhole boards.
# It assumes the following:
# - pyocd is installed and available in the PATH
# - tt-flash is installed and available in the PATH
# - The DUT is connected and accessible via JTAG
# - All required firmware artifacts are available in the specified directories

set -e

TT_Z_P_ROOT=$(realpath "$(dirname "$(realpath "$0")")"/../..)

function print_help {
	echo "Usage: $(basename "$0") [options] <board_name>"
	echo ""
	echo "Run the manufacturing test sequence for a Blackhole board."
	echo ""
	echo "Options:"
	echo "  -p <dir>   Directory containing preflash ihex (default: artifacts/preflash)"
	echo "  -m <dir>   Directory containing assembly MCUBoot ELF"
	echo "             (default: artifacts/assembly-mcuboot)"
	echo "  -a <dir>   Directory containing assembly app hex (default: artifacts/assembly-fw)"
	echo "  -f <dir>   Directory containing production fwbundle (default: artifacts/fwbundle)"
	echo "  -h         Show this help"
}

if [ $# -lt 1 ]; then
	print_help
	exit 1
fi

PREFLASH_DIR="artifacts/preflash"
MCUBOOT_DIR="artifacts/assembly-mcuboot"
ASSEMBLY_FW_DIR="artifacts/assembly-fw"
FWBUNDLE_DIR="artifacts/fwbundle"

while getopts "p:m:a:f:h" opt; do
	case "$opt" in
		p) PREFLASH_DIR=$OPTARG ;;
		m) MCUBOOT_DIR=$OPTARG ;;
		a) ASSEMBLY_FW_DIR=$OPTARG ;;
		f) FWBUNDLE_DIR=$OPTARG ;;
		h) print_help; exit 0 ;;
		\?) print_help; exit 1 ;;
	esac
done
shift $((OPTIND-1))

BOARD=$1

if [ -z "$BOARD" ]; then
	echo "ERROR: Board name is required"
	print_help
	exit 1
fi

# Derive board properties
ASSEMBLY_BOARD=$(echo "$BOARD" | sed 's/[a-z]$//')
# p300 variants have 2 ASICs, all others have 1
if [[ "$ASSEMBLY_BOARD" == p300 ]]; then
	NUM_ASICS=2
else
	NUM_ASICS=1
fi

echo "Board: $BOARD, Assembly board: $ASSEMBLY_BOARD, Num ASICs: $NUM_ASICS"
echo "Preflash dir: $PREFLASH_DIR"
echo "MCUBoot dir: $MCUBOOT_DIR"
echo "Assembly FW dir: $ASSEMBLY_FW_DIR"
echo "FW bundle dir: $FWBUNDLE_DIR"

function verify_pcie_enumeration {
	local DESCRIPTION=$1
	echo "Verifying PCIe enumeration ($DESCRIPTION)..."
	local TIMEOUT=60
	local ELAPSED=0
	while true; do
		local COUNT=0
		for dev in /sys/bus/pci/devices/*/vendor; do
			if [ -f "$dev" ] && [ "$(cat "$dev")" = "0x1e52" ]; then
				COUNT=$((COUNT + 1))
			fi
		done
		echo "Detected $COUNT Tenstorrent PCIe endpoint(s), expected $NUM_ASICS"
		if [ "$COUNT" -ge "$NUM_ASICS" ]; then
			echo "PASS: All $NUM_ASICS endpoint(s) enumerated"
			return 0
		fi
		if [ "$ELAPSED" -ge "$TIMEOUT" ]; then
			echo "FAIL: Only $COUNT of $NUM_ASICS endpoint(s)" \
				"enumerated after ${TIMEOUT}s"
			return 1
		fi
		echo "Rescanning PCIe bus..."
		echo 1 > /sys/bus/pci/rescan
		sleep 2
		ELAPSED=$((ELAPSED + 2))
	done
}

# ---- Step 1: Erase SPI + flash preflash ihex ----
echo "=== Step 1: Erase SPI and flash preflash ihex ==="
PREFLASH_IHEX=$(ls "$PREFLASH_DIR"/preflash-*.ihex | head -1)
echo "Using preflash image: $PREFLASH_IHEX"
python3 "$TT_Z_P_ROOT"/scripts/spi_flash.py \
	--board-name "$BOARD" --no-prompt -v full_erase
python3 "$TT_Z_P_ROOT"/scripts/spi_flash.py \
	--board-name "$BOARD" --no-prompt -v \
	write_from_ihex "$PREFLASH_IHEX"

# ---- Step 2: Flash assembly test FW to DMC via pyocd ----
echo "=== Step 2: Flash assembly test firmware to DMC ==="
MCUBOOT_ELF="$MCUBOOT_DIR/zephyr.elf"
APP_HEX="$ASSEMBLY_FW_DIR/zephyr.signed.hex"

echo "Flashing MCUBoot bootloader: $MCUBOOT_ELF"
echo "Flashing assembly test app: $APP_HEX"

if [ ! -f "$MCUBOOT_ELF" ]; then
	echo "ERROR: MCUBoot ELF not found at $MCUBOOT_ELF"
	exit 1
fi
if [ ! -f "$APP_HEX" ]; then
	echo "ERROR: Assembly test app hex not found at $APP_HEX"
	exit 1
fi
python3 "$TT_Z_P_ROOT"/scripts/dmc_flash.py --no-prompt -v \
	flash "$APP_HEX" "$MCUBOOT_ELF"

# ---- Step 3: DMC reset (simulate power-on) ----
echo "=== Step 3: DMC reset after assembly flash ==="
python3 "$TT_Z_P_ROOT"/scripts/dmc_reset.py

# ---- Step 4: Verify PCIe enumeration (assembly test FW + preflash) ----
echo "=== Step 4: Verify PCIe enumeration (assembly test FW) ==="
verify_pcie_enumeration "assembly test FW"

# ---- Step 5: Flash production FW via tt-flash ----
echo "=== Step 5: Flash production firmware ==="
FWBUNDLE=$(ls "$FWBUNDLE_DIR"/fw_pack*.fwbundle | head -1)
echo "Using firmware bundle: $FWBUNDLE"
tt-flash "$FWBUNDLE" --force

# ---- Step 6: DMC reset (simulate cold boot after bootstrap) ----
echo "=== Step 6: DMC reset after production flash ==="
python3 "$TT_Z_P_ROOT"/scripts/dmc_reset.py

# ---- Step 7: Verify PCIe enumeration (production FW) ----
echo "=== Step 7: Verify PCIe enumeration (production FW) ==="
verify_pcie_enumeration "production FW"

echo "=== Manufacturing test completed successfully! ==="
