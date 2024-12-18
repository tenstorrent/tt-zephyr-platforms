#!/bin/bash

# Copyright (c) 2024 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

set -e

TT_PCI_VID=1e52

echo "Preparing to restore BMFW to known good image.."

export PATH=$PATH:/opt/SEGGER/JLink

# function to call tee with sudo, if necessary
stee() {
  local STEE="sudo tee"

  if [ $UID -eq 0 ]; then
    STEE="tee"
  fi

  # here we cheat a little.
  # cannot access /sys/bus/pci in docker :sob:, so we ignore any errors until we have a better solution. This works outside of a
  # docker container :shrug:
  $STEE $* || true
}

# function to remove the PCIe device
pciremove() {
  # lspci is not in this docker image, so look for (1) TT device via sysfs
  for d in /sys/bus/pci/devices/*; do
    if [ "$(cat $d/vendor)" = "0x${TT_PCI_VID}" ]; then
      echo "Removing PCI device $d with vendor id ${TT_PCI_VID}"
      echo 1 | stee $d/remove
      break
    fi
  done
}

# function to rescan the PCIe bus
pcirescan() {
  echo "Rescanning the PCI bus"
  echo 1 | stee /sys/bus/pci/rescan
}

if [ "$BOARD" = "" ]; then
  echo "ERROR: BOARD must be set to flash BMC FW back to main"
  exit 1
fi

if [ ! -d "/opt/tenstorrent/fw/stable/${BOARD}" ]; then
  echo "Skipping restore, since /opt/tenstorrent/fw/stable/${BOARD} does not exist"
  exit 0
fi

# Restore known-good firmware to the BMC, so the device re-enumerates on the PCIe bus.
pciremove
west flash --skip-rebuild -d "/opt/tenstorrent/fw/stable/${BOARD}"
pcirescan
