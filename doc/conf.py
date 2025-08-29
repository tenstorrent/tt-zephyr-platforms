# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path

TTZP = Path(__file__).parent.parent

project = "TT Zephyr Platforms"
copyright = "2025, Tenstorrent AI ULC"
author = "Tenstorrent AI ULC"
release = "1.0.0"
extensions = [
    "sphinx.ext.intersphinx",
    "sphinx_rtd_theme",
    "sphinx_tabs.tabs",
]
templates_path = ["_templates"]
exclude_patterns = ["_build_sphinx", "Thumbs.db", ".DS_Store"]
html_theme = "sphinx_rtd_theme"
external_content_contents = [
    (TTZP / "doc", "[!_]*"),
    (TTZP, "boards/**/*.rst"),
]
intersphinx_mapping = {"zephyr": ("https://docs.zephyrproject.org/latest/", None)}
