# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import pytest


def pytest_addoption(parser):
    parser.addoption(
        "--asic-id", action="store", default=0, help="Set ASIC IDX in use", type=int
    )


@pytest.fixture(scope="session")
def asic_id(request):
    return request.config.getoption("--asic-id")
