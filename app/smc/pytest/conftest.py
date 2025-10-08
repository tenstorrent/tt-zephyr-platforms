# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import logging
from pathlib import Path
import pytest
import sys

# Add the scripts directory to sys.path to import recovery script
sys.path.append(str(Path(__file__).resolve().parents[3] / "scripts"))
import smc_test_recovery

logger = logging.getLogger(__name__)


def pytest_addoption(parser):
    parser.addoption(
        "--asic-id", action="store", default=0, help="Set ASIC IDX in use", type=int
    )


@pytest.fixture(scope="session")
def asic_id(request):
    return request.config.getoption("--asic-id")


def pytest_exception_interact(node, call, report):
    if report.failed:
        asic_id = node.config.getoption("--asic-id")
        logger.error(f"Test failed, running recovery for ASIC ID {asic_id}")
        try:
            smc_test_recovery.recover_smc(asic_id)
        except Exception as e:
            logger.error(f"Recovery failed: {e}")
