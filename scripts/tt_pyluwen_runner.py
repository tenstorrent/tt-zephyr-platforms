# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0

from runners.core import RunnerCaps, ZephyrBinaryRunner
import os
import subprocess

class TTPyluwenRunner(ZephyrBinaryRunner):
    def __init__(self, cfg, pyluwen_port, asic_id):
        super().__init__(cfg)
        self.gdb_port = pyluwen_port
        self.asic_id = asic_id
        self.gdb = cfg.gdb or 'arc-zephyr-elf-gdb'
        self.elf = cfg.elf_file
        self.pyluwen_script = os.path.join(os.path.dirname(__file__), 'pyluwen_gdb_remote.py')

    @classmethod
    def name(cls):
        return "tt_pyluwen"

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={"attach"})

    @classmethod
    def do_add_parser(cls, parser):
        parser.add_argument('--port', type=int, default=2159,
                            help='TCP port for pyluwen GDB server (default: 2159)')
        parser.add_argument('--asic-id', type=int, default=0,
                            help='ASIC ID for pyluwen (default: 0)')

    def do_attach(self, **kwargs):
        self.do_run_server_and_gdb()

    def do_debug(self, **kwargs):
        self.do_run_server_and_gdb()

    def do_run_server_and_gdb(self):
        # Start the pyluwen GDB remote server
        server_cmd = [
            'python3', self.pyluwen_script,
            '--port', str(self.gdb_port),
            '--asic-id', str(self.asic_id)
        ]
        print(f"Starting pyluwen GDB server: {' '.join(server_cmd)}")
        server_proc = subprocess.Popen(server_cmd)

        # Wait a moment for the server to start
        import time
        time.sleep(1)

        # Start GDB and attach
        gdb_cmd = [self.gdb, self.elf, '-ex', f'target remote localhost:{self.gdb_port}']
        print(f"Starting GDB: {' '.join(gdb_cmd)}")
        subprocess.call(gdb_cmd)

        # When GDB exits, terminate the server
        server_proc.terminate()
        server_proc.wait()

    @classmethod
    def do_create(cls, cfg, args):
        return cls(cfg, pyluwen_port=args.port, asic_id=args.asic_id)

    def do_run(self, command, **kwargs):
        self.cfg_cmd = []
        if command in ('attach'):
            self.do_run_server_and_gdb()
