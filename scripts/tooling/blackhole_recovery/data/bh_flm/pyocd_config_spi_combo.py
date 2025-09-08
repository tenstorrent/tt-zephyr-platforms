# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import sys

sys.path.append(str(Path(__file__).parent))
import pyocd_shared


def will_connect():
    """
    Called by pyocd at target connection time.
    """
    flm = Path(__file__).parent / "build" / "spi_combo.flm"
    # Target variable is defined by pyocd module
    pyocd_shared.will_connect(flm, target)  # pylint: disable=undefined-variable # noqa: F821


def did_connect():
    """
    Called by pyocd after target connection.
    """
    # Target variable is defined by pyocd module
    pyocd_shared.did_connect(target)  # pylint: disable=undefined-variable # noqa: F821
