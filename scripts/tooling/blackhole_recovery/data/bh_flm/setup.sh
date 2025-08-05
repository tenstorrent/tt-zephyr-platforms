#!/bin/sh

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

set -e

cd $(dirname "$(realpath "$0")")

pyocd pack install STM32G0B1CEUx

REPO="danieldegrasse"
REV="c616c215faebc885e6733c730e22d1d267f6aa04"

curl -L "https://raw.githubusercontent.com/$REPO/FlashAlgo/$REV/STM32G0Bx_SPI_EEPROM.FLM" \
  -o STM32G0Bx_SPI_EEPROM.FLM
