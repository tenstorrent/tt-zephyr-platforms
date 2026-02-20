# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0

"""
Shared pyocd / ST-Link helpers for Blackhole PCIe card tooling.

Consolidates probe-session creation, ST-Link USB recovery, and board
metadata loading that were previously duplicated across spi_flash.py,
recover-blackhole.py, tt_bootstrap.py, and set-p300-jtag.py.
"""

import logging
import os
import sys
from pathlib import Path

import yaml

try:
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader

try:
    import usb.core
    from pyocd.core.helpers import ConnectHelper
    from pyocd.core import exceptions as pyocd_exceptions
except ImportError:
    print("Required modules not found. Please run: pip install pyocd pyusb")
    sys.exit(os.EX_UNAVAILABLE)

logger = logging.getLogger(__name__)

PYOCD_TARGET = "STM32G0B1CEUx"
PYOCD_FLM_PATH = Path(__file__).parent / "tooling/blackhole_recovery/data/bh_flm"
BOARD_METADATA_PATH = Path(__file__).parent / "board_metadata.yaml"


def load_board_metadata(path=None):
    """Load board_metadata.yaml and return the parsed dict.

    Args:
        path: Optional override for the metadata file location.
              Defaults to ``BOARD_METADATA_PATH``.
    """
    path = Path(path) if path else BOARD_METADATA_PATH
    with open(path) as f:
        return yaml.load(f.read(), Loader=SafeLoader)


def recover_stlink():
    """Attempt to recover ST-Link probes by resetting the USB bus."""

    def is_stlink_device(dev):
        return dev.idVendor == 0x0483  # STMicroelectronics

    stlink_devs = usb.core.find(find_all=True, custom_match=is_stlink_device)
    for dev in stlink_devs:
        try:
            dev.reset()
            logger.info(
                "Reset ST-Link device: VID=%04x, PID=%04x",
                dev.idVendor,
                dev.idProduct,
            )
        except usb.core.USBError as e:
            logger.warning(
                "Error resetting ST-Link device: VID=%04x, PID=%04x, %s",
                dev.idVendor,
                dev.idProduct,
                e,
            )


def create_session(pyocd_config=None, adapter_id=None, no_prompt=False):
    """Create a pyocd session for the STM32 target.

    Args:
        pyocd_config: Path to a pyocd user-script (e.g. a FLM config).
                      May be ``None`` for sessions that don't need SPI flash.
        adapter_id:   ST-Link serial / unique ID.  ``None`` = auto-select.
        no_prompt:    When *True* (or when stdin is not a tty), pick the
                      first available probe without prompting.

    Returns:
        A pyocd ``Session`` object (not yet opened).

    Raises:
        RuntimeError: if no probe could be found.
    """
    kwargs = {"target_override": PYOCD_TARGET}
    if pyocd_config is not None:
        kwargs["user_script"] = str(pyocd_config)

    if adapter_id is not None:
        kwargs["unique_id"] = adapter_id
    elif no_prompt or not sys.stdin.isatty():
        logger.info("No adapter ID provided, selecting first available debug probe")
        kwargs["return_first"] = True
    else:
        logger.info(
            "No adapter ID provided, please select the debugger "
            "attached to STM32 if prompted"
        )

    session = ConnectHelper.session_with_chosen_probe(**kwargs)
    if session is None:
        raise RuntimeError("Failed to connect to the debug probe")
    return session


def get_session(pyocd_config=None, adapter_id=None, no_prompt=False):
    """Like :func:`create_session`, but retries once after an ST-Link
    USB reset if the initial connection fails.
    """
    try:
        return create_session(pyocd_config, adapter_id, no_prompt)
    except pyocd_exceptions.ProbeError as e:
        logger.warning("Error connecting to probe, will attempt USB reset: %s", e)
        recover_stlink()
        return create_session(pyocd_config, adapter_id, no_prompt)
