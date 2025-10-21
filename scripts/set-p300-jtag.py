#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0


import argparse
import sys

try:
    from pyocd.core.helpers import ConnectHelper
except ImportError:
    print("Required module 'pyocd' not found. Please run 'pip install pyocd'.")
    sys.exit(1)

PYOCD_TARGET = "STM32G0B1CEUx"

# Port D gpio registers on STM32G0
JTAG_GPIO_REG_MODER = 0x50000C00
JTAG_GPIO_REG_PUPDR = 0x50000C0C
JTAG_GPIO_REG_BSSR = 0x50000C18

# ARC_JTAG pin: PD13
ARC_JTAG_PIN = 13
# SYS_JTAG pin: PD14
SYS_JTAG_PIN = 14


def parse_args():
    parser = argparse.ArgumentParser(
        description="Select JTAG mux for P300 board.", allow_abbrev=False
    )
    parser.add_argument(
        "jtag_type",
        type=str,
        choices=["ARC", "SYS"],
        help="JTAG mux to configure: 'ARC' for ARC core, 'SYS' for system JTAG",
    )
    parser.add_argument("asic", type=int, choices=[0, 1], help="ASIC number (0 or 1)")
    parser.add_argument(
        "--adapter-id",
        type=str,
        help="Adapter ID for the ST-Link device used in recovery",
    )
    parser.add_argument(
        "--no-prompt",
        default=False,
        help="Do not prompt for adapter if none is provided, use first available",
        action="store_true",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    print("Connecting to STM32 debug probe, select it if prompted...")
    session = ConnectHelper.session_with_chosen_probe(
        target_override=PYOCD_TARGET,
        unique_id=args.adapter_id,
        prompt=(not args.no_prompt),
    )

    if session is None:
        print("No probe found. Please connect a debug probe and try again.")
        sys.exit(1)

    with session:
        target = session.target
        target.resume()
        # Set pin to output mode
        if args.jtag_type == "ARC":
            print("Configuring JTAG mux for ARC...")
            pin = ARC_JTAG_PIN
        else:
            print("Configuring JTAG mux for SYS...")
            pin = SYS_JTAG_PIN

        moder = target.read32(JTAG_GPIO_REG_MODER)
        moder &= ~(0x3 << (pin * 2))
        moder |= 0x1 << (pin * 2)
        target.write32(JTAG_GPIO_REG_MODER, moder)
        # No pull-up/pull-down
        pupdr = target.read32(JTAG_GPIO_REG_PUPDR)
        pupdr &= ~(0x3 << (pin * 2))
        target.write32(JTAG_GPIO_REG_PUPDR, pupdr)
        if args.asic == 1:
            # Set pin high
            target.write32(JTAG_GPIO_REG_BSSR, (1 << pin))
        else:
            # Set pin low
            target.write32(JTAG_GPIO_REG_BSSR, (1 << (pin + 16)))
        print(f"{args.jtag_type} JTAG mux configured for ASIC {args.asic}.")


if __name__ == "__main__":
    main()
