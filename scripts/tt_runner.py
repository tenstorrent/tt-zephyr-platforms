# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0

# Testing with
# west twister -i -c -p tt_blackhole@p100/tt_blackhole/smc --device-testing --west-runner tt_runner --device-serial-pty /opt/tenstorrent/bin/tt-console --flash-before --west-flash="--erase" --tag gpio --tag smoke  --alt-config-root test-conf/tests -T ../zephyr/tests -s drivers.gpio.1pin -X gpio_external_pull_down

from runners.core import RunnerCaps, ZephyrBinaryRunner
from pathlib import Path


class TTRunner(ZephyrBinaryRunner):
    def __init__(self, cfg, args):
        super().__init__(cfg)
        self.logger.info(f'cfg: {cfg} args: {args}')

    @classmethod
    def name(cls):
        return "tt_runner"

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={"flash"}, dev_id=True, erase=True, file=True, reset=True)

    @classmethod
    def do_add_parser(cls, parser):
        # mainly for handling -O options
        pass

    @classmethod
    def do_create(cls, cfg, args):
        # check for --dt-flash, --erase, --file, --reset
        return cls(cfg, args)

    def do_run(self, command, **kwargs):

        self.logger.info(f'command: {command} kwargs: {kwargs}')
