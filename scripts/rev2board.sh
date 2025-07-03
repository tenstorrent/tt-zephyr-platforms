#!/bin/sh

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

CLUSTER="smc"

if [ $# -eq 0 ]; then
  >&2 echo "Usage: $0 <board> [<cluster>]"
  >&2 echo "Example: $0 p100 -> tt_blackhole@p100/tt_blackhole/smc"
  >&2 echo "Example: $0 p100 smc -> tt_blackhole@p100/tt_blackhole/smc"
  >&2 echo "Example: $0 p100 dmc -> tt_blackhole@p100/tt_blackhole/dmc"
  exit 1
fi

if [ $# -ge 2 ]; then
  CLUSTER="$2"
fi

case "$1" in
  p100) BOARD="tt_blackhole@p100/tt_blackhole/$CLUSTER";;
  p100a) BOARD="tt_blackhole@p100a/tt_blackhole/$CLUSTER";;
  p150a) BOARD="tt_blackhole@p150a/tt_blackhole/$CLUSTER";;
  p150b) BOARD="tt_blackhole@p150b/tt_blackhole/$CLUSTER";;
  p150c) BOARD="tt_blackhole@p150c/tt_blackhole/$CLUSTER";;
  p300a) BOARD="tt_blackhole@p300a/tt_blackhole/$CLUSTER";;
  p300b) BOARD="tt_blackhole@p300b/tt_blackhole/$CLUSTER";;
  p300c) BOARD="tt_blackhole@p300c/tt_blackhole/$CLUSTER";;
  galaxy) BOARD="tt_blackhole@galaxy/tt_blackhole/$CLUSTER";;
  *) >&2 echo "Unknown board: $1"; exit 1;;
esac

echo "$BOARD"
