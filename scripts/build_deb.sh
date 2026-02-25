#!/bin/sh

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

# Ubuntu requirements:
# sudo apt-get install dpkg-dev debsigs gnupg

# Homebrew requirements (on macOS - note, no debsigs available):
# brew install dpkg

set -e

BUILD_DIR="$(mktemp -d)"

cleanup() {
  # remove temporary build directory if it exists
  if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
  fi
}
trap cleanup EXIT

# https://www.debian.org/doc/manuals/debian-reference/ch02.en.html#_debian_package_file_names
# package-name_version-distro.revision_architecture.deb
DEFAULT_PACKAGE_NAME="tt-firmware"
VERSION=""
DEFAULT_DISTRO="ubuntu"
DEFAULT_REV="1"
DEFAULT_URL="https://github.com/tenstorrent/tt-system-firmware"

PACKAGE_NAME="$DEFAULT_PACKAGE_NAME"
DISTRO="$DEFAULT_DISTRO"
REV="$DEFAULT_REV"
ARCH="all"
# Note: even though our firmware is mostly Open Source, there are some proprietary blobs :(
SECTION="non-free-firmware"
PRIORITY="optional"
MAINT_EMAIL="releases@tenstorrent.com"
MAINT_NAME="Tenstorrent Releases"
URL="$DEFAULT_URL"

# if an error occurred
ERROR=0
# .fwbundle file (-b file, required)
FWBUNDLE=""
# print help message (-h, optional)
HELP=0
# deb signing key
DEBKEY=""
# license file (-l file, optional, default = None)
LICENSE_FILE=""
# output directory (-O dir, optional, default = /tmp)
DEFAULT_OUTPUT_DIR="/tmp"
OUTPUT_DIR="$DEFAULT_OUTPUT_DIR"
# SPDX file (-s file, optional, default = None)
SPDX=""

CONTROL_FILE="$BUILD_DIR/DEBIAN/control"

help() {
cat << EOF
Usage: $(basename "$0") [options]
Options:
  -b FILE     .fwbundle file (required)
  -d DISTRO   target distro (default: $DEFAULT_DISTRO)
  -h          display this help message
  -k FILE     deb signing key (required)
  -l FILE     license file to include in package (optional)
  -o DIR      output directory (default: $DEFAULT_OUTPUT_DIR)
  -p NAME     package name (default: $DEFAULT_PACKAGE_NAME)
  -r REV      package revision (default: $DEFAULT_REV)
  -s FILE     SPDX file to include in package (optional)
  -u URL      VCS URL (default: $DEFAULT_URL)
  -v VERSION  package version (required)
EOF
}

while getopts "b:d:hk:l:o:p:r:s:u:v:" opt; do
  case $opt in
    b)
      FWBUNDLE="$(realpath "$OPTARG")"
      ;;
    d)
      DISTRO="$OPTARG"
      ;;
    h)
      HELP=1
      ;;
    k)
      DEBKEY="$OPTARG"
      ;;
    l)
      LICENSE_FILE="$(realpath "$OPTARG")"
      ;;
    o)
      if [ ! -d "$OPTARG" ]; then
        mkdir -p "$OPTARG"
      fi
      OUTPUT_DIR="$(realpath "$OPTARG")"
      ;;
    p)
      PACKAGE_NAME="$OPTARG"
      ;;
    r)
      REV="$OPTARG"
      ;;
    s)
      SPDX="$(realpath "$OPTARG")"
      ;;
    u)
      URL="$OPTARG"
      ;;
    v)
      VERSION="$OPTARG"
      ;;
    \?)
      echo "Invalid option: -$opt" >&2
      ERROR=1
      ;;
    :)
      echo "Option -$OPTARG requires an argument." >&2
      ERROR=1
      ;;
  esac
done

# check for help
if [ $HELP -eq 1 ]; then
  help
  exit 0
fi

# check if getopts found errors
if [ $ERROR -ne 0 ]; then
  help
  exit 1
fi

# TODO: more argument validation
if [ -z "$FWBUNDLE" ] || [ -z "$VERSION" ] || [ -z "$DEBKEY" ]; then
  echo "Error: Missing required arguments." >&2
  help
  exit 1
fi

# output file
OUTPUT_PACKAGE="$OUTPUT_DIR/${PACKAGE_NAME}_${VERSION}-${DISTRO}.${REV}_${ARCH}.deb"
mkdir -p "$OUTPUT_DIR"
rm -f "$OUTPUT_PACKAGE" >/dev/null 2>&1

# control file
mkdir -p "$BUILD_DIR/DEBIAN"
cat << EOF > "$CONTROL_FILE"
Package: $PACKAGE_NAME
Version: $VERSION
Section: $SECTION
Priority: $PRIORITY
Architecture: $ARCH
Maintainer: $MAINT_NAME <$MAINT_EMAIL>
Description: Tenstorrent firmware
Vcs-Browser: $URL
Vcs-Git: $URL -b $VERSION
EOF

# note: it would be great to use install(1) but it doesn't have the same -D option on macOS

# package contents
mkdir -p "$BUILD_DIR/lib/firmware/tenstorrent/$VERSION"
install -m 644 "$FWBUNDLE" "$BUILD_DIR/lib/firmware/tenstorrent/$VERSION/$(basename "$FWBUNDLE")"
if [ -n "$LICENSE_FILE" ]; then
  install -m 644 "$LICENSE_FILE" "$BUILD_DIR/lib/firmware/tenstorrent/$VERSION/LICENSE"
fi
if [ -n "$SPDX" ]; then
  install -m 644 "$SPDX" "$BUILD_DIR/lib/firmware/tenstorrent/$VERSION/$(basename "$SPDX")"
fi

# create a minimal changelog to satisfy lintian
cat << EOF > "$BUILD_DIR/DEBIAN/changelog"
$PACKAGE_NAME ($VERSION-$REV) $DISTRO; urgency=low

  * See $URL/releases/tag/$VERSION for release details

 -- $MAINT_NAME <$MAINT_EMAIL>  $(date -R)
EOF

# build the deb
dpkg-deb --build --root-owner-group "$BUILD_DIR" "$OUTPUT_PACKAGE"

if [ "$(which debsigs 2>/dev/null || true)" = "" ]; then
  echo "Warning: debsigs not found, skipping deb signing." >&2
  exit 0
fi

# sign the deb
debsigs --sign=builder -k "$DEBKEY" "$OUTPUT_PACKAGE"

# TODO: verify the signed deb? (needs ppa installed, etc)
# debsigs --verify "$OUTPUT_PACKAGE"
