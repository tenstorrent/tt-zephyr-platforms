# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import sys

from pathlib import Path

TTZP = Path(__file__).parent.parent
ZEPHYR_BASE = TTZP.parent / "zephyr"

# Add the '_extensions' directory to sys.path, to enable finding Sphinx
# extensions within.
sys.path.insert(0, str(ZEPHYR_BASE / "doc" / "_extensions"))

# Add the '_scripts' directory to sys.path, to enable finding utility
# modules.
sys.path.insert(0, str(ZEPHYR_BASE / "doc" / "_scripts"))

sys.path.insert(0, str(TTZP / "scripts"))

from get_ttzp_version import get_ttzp_version  # noqa: E402

project = "TT Zephyr Platforms"
copyright = "2025, Tenstorrent AI ULC"
author = "Tenstorrent AI ULC"
release = get_ttzp_version()
extensions = [
    "myst_parser",
    "sphinx.ext.intersphinx",
    "sphinx_rtd_theme",
    "sphinx_tabs.tabs",
    "zephyr.application",
]
templates_path = [str(ZEPHYR_BASE / "doc/_templates")]
exclude_patterns = ["_build_sphinx", "Thumbs.db", ".DS_Store"]
html_theme = "sphinx_rtd_theme"
external_content_contents = [
    (TTZP / "doc", "[!_]*"),
    (TTZP, "boards/**/*.rst"),
]

html_context = {
    "project": project,
    "reference_links": {"API": "doxygen/index.html"},
}


intersphinx_mapping = {"zephyr": ("https://docs.zephyrproject.org/latest/", None)}
source_suffix = {
    ".rst": "restructuredtext",
    ".md": "markdown",
}
