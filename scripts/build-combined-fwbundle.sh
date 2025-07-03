#!/bin/sh

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

set -e

if [ -z "$BUNDLE_TEMP_PREFIX" ]; then
  TEMP_DIR="$(mktemp -d)/"
  echo "Created TEMP_DIR"
else
  echo "Using existing BUNDLE_TEMP_PREFIX: $BUNDLE_TEMP_PREFIX"
  TEMP_DIR="$BUNDLE_TEMP_PREFIX"
fi

cleanup() {
    if [ -z "$BUNDLE_TEMP_PREFIX" ] && [ -d "$TEMP_DIR" ]; then
        rm -Rf "$TEMP_DIR"
        echo "Removed TEMP_DIR"
    fi
}
trap cleanup EXIT

mPATH="$(dirname "$0")"
TTZP_BASE="$(dirname "$mPATH")"
# put tt_boot_fs.py in the path (use it to combine .fwbundle files)
PATH="$mPATH:$PATH"

# TODO: read boards / board revs from a YAML file
BOARD_REVS="$(jq -r -c ".[]" "$TTZP_BASE/.github/boards.json")"

MAJOR="$(grep "^VERSION_MAJOR" "$TTZP_BASE/VERSION" | awk '{print $3}')"
MINOR="$(grep "^VERSION_MINOR" "$TTZP_BASE/VERSION" | awk '{print $3}')"
PATCH="$(grep "^PATCHLEVEL" "$TTZP_BASE/VERSION" | awk '{print $3}')"
EXTRAVERSION="$(grep "^EXTRAVERSION" "$TTZP_BASE/VERSION" | awk '{print $3}')"

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

rev2board() {
  case "$1" in
    p100) BOARD=tt_blackhole@p100/tt_blackhole/smc;;
    p100a) BOARD=tt_blackhole@p100a/tt_blackhole/smc;;
    p150a) BOARD=tt_blackhole@p150a/tt_blackhole/smc;;
    p150b) BOARD=tt_blackhole@p150b/tt_blackhole/smc;;
    p150c) BOARD=tt_blackhole@p150c/tt_blackhole/smc;;
    p300a) BOARD=tt_blackhole@p300a/tt_blackhole/smc;;
    p300b) BOARD=tt_blackhole@p300b/tt_blackhole/smc;;
    p300c) BOARD=tt_blackhole@p300c/tt_blackhole/smc;;
    galaxy) BOARD=tt_blackhole@galaxy/tt_blackhole/smc;;
    *) echo "Unknown board: $1"; exit 1;;
  esac

  echo "$BOARD"
}

for REV in $BOARD_REVS; do
  BOARD="$(rev2board "$REV")"

  if [ -n "$BUNDLE_TEMP_PREFIX" ]; then
    if [ -f "${TEMP_DIR}${REV}/update.fwbundle" ]; then
      echo "Using pre-built ${TEMP_DIR}${REV}/update.fwbundle"
      continue
    fi
    echo "Warning: pre-built ${TEMP_DIR}${REV}/update.fwbundle not found"
  fi

  echo "Building $BOARD"
  west build -d "${TEMP_DIR}${REV}" --sysbuild -p -b "$BOARD" app/smc \
    -DEXTRA_CONF_FILE=vuart.conf \
    -DEXTRA_DTC_OVERLAY_FILE=vuart.overlay \
    >/dev/null 2>&1
done

echo "Creating fw_pack-$RELEASE.fwbundle"
# construct arguments..
ARGS="-c $PWD/$TTZP_BASE/zephyr/blobs/fw_pack-grayskull.tar.gz"
ARGS="$ARGS -c $PWD/$TTZP_BASE/zephyr/blobs/fw_pack-wormhole.tar.gz"
for REV in $BOARD_REVS; do
  ARGS="$ARGS -c ${TEMP_DIR}${REV}/update.fwbundle"
done

# shellcheck disable=SC2086
tt_boot_fs.py fwbundle \
  -v "$PRELEASE" \
  -o "fw_pack-$RELEASE.fwbundle" \
  $ARGS
