#!/bin/sh

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

set -e

TEMP_DIR="$(mktemp -d)"

echo "Created TEMP_DIR"

cleanup() {
    if [ -d "$TEMP_DIR" ]; then
        rm -Rf "$TEMP_DIR"
        echo "Removed TEMP_DIR"
    fi
}
trap cleanup EXIT

mPATH="$(dirname "$0")"
# put tt_boot_fs.py in the path (use it to combine .fwbundle files)
PATH="$mPATH:$PATH"

BOARD_REVS="p100 p100a p150a p150b p150c p300a p300b p300c"

MAJOR="$(grep "^VERSION_MAJOR" VERSION | awk '{print $3}')"
MINOR="$(grep "^VERSION_MINOR" VERSION | awk '{print $3}')"
PATCH="$(grep "^PATCHLEVEL" VERSION | awk '{print $3}')"
EXTRAVERSION="$(grep "^EXTRAVERSION" VERSION | awk '{print $3}')"

if [ -z "$MAJOR" ] || [ -z "$MINOR" ] || [ -z "$PATCH" ]; then
  echo "required version info missing: MAJOR=$MAJOR MINOR=$MINOR PATCH=$PATCH"
  exit 1
fi

if [ -n "$EXTRAVERSION" ]; then
  EXTRAVERSION="-$EXTRAVERSION"
  EXTRAVERSION_NUMBER="$(echo "$EXTRAVERSION" | sed 's/[^0-9]*//g')"
else
  EXTRAVERSION_NUMBER=0
fi

RELEASE="$MAJOR.$MINOR.$PATCH$EXTRAVERSION"
PRELEASE="$MAJOR.$MINOR.$PATCH.$EXTRAVERSION_NUMBER"

echo "Building release $RELEASE / pack $PRELEASE"

for REV in $BOARD_REVS; do
  BOARD="tt_blackhole@$REV/tt_blackhole/smc";;

  echo "Building $BOARD"
  west build -d "$TEMP_DIR/$REV" --sysbuild -p -b "$BOARD" app/smc >/dev/null 2>&1
done

echo "Creating fw_pack-$PRELEASE.fwbundle"
# construct arguments..
ARGS=""
for REV in $BOARD_REVS; do
  ARGS="$ARGS -c $TEMP_DIR/$REV/update.fwbundle"
done

# shellcheck disable=SC2086
tt_boot_fs.py fwbundle \
  -v "$PRELEASE" \
  -o "fw_pack-$PRELEASE.fwbundle" \
  $ARGS
