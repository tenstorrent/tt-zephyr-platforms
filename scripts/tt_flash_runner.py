# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0

from runners.core import RunnerCaps, ZephyrBinaryRunner
from pathlib import Path


class TTFlashRunner(ZephyrBinaryRunner):
    """Runner for the tt_flash command"""

    def __init__(self, cfg, force, tt_flash):
        super().__init__(cfg)
        self.force = force
        # If file is passed, flash that. Otherwise flash update.fwbundle
        # in build dir
        if cfg.file:
            self.file = Path(cfg.file)
        else:
            self.file = Path(cfg.build_dir).parent / "update.fwbundle"
        self.tt_flash = tt_flash

    @classmethod
    def name(cls):
        return "tt_flash"

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={"flash"}, file=True)

    @classmethod
    def do_add_parser(cls, parser):
        parser.add_argument("--force", action="store_true", help="Force flash")
        parser.add_argument("--tt-flash", default="tt-flash", help="Path to tt-flash")

    @classmethod
    def do_create(cls, cfg, args):
        return cls(cfg, force=args.force, tt_flash=args.tt_flash)

    def do_run(self, command, **kwargs):
        self.tt_flash = self.require(self.tt_flash)
        if not self.file.exists():
            raise RuntimeError(f"File {self.file} does not exist")
        cmd = [self.tt_flash, "flash", "--fw-tar", str(self.file)]
        if self.force:
            cmd.append("--force")
        self.logger.debug(f"Running {cmd}")
        self.check_call(cmd)
