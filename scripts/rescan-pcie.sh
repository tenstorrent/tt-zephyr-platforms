#!/bin/bash

# Copyright (c) 2024 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

set -e

TT_PCI_VID=1e52

# function to call tee with sudo, if necessary
stee() {
  local STEE="sudo tee"

  if [ $UID -eq 0 ]; then
    STEE="tee"
  fi

  $STEE $*
}

# function to remove the PCIe device
pciremove() {
  # look for (1) TT device via sysfs
  for d in /sys/bus/pci/devices/*; do
    if [ "$(cat $d/vendor)" = "0x${TT_PCI_VID}" ]; then
      echo "Removing PCI device $d with vendor id ${TT_PCI_VID}"
      echo 1 | stee $d/remove
    fi
  done
}

# function to rescan the PCIe bus
pcirescan() {
  echo "Rescanning the PCI bus"
  echo 1 | stee /sys/bus/pci/rescan
}

pciremove
pcirescan
