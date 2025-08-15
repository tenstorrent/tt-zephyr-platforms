# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0

"""
This runner is used to load firmware directly into the SPI flash of a PCIe card.
This direct loading method does not require functional firmware on the card,
so it will not read back the board ID. Instead, the runner will reprogram
the ID in flash.
It supports flashing hex files directly, or extracting firmware from a
firmware bundle. If a hex file is provided, only one ASIC on a board can be flashed
at a time.
"""

from pathlib import Path
import sys
import time
import tarfile
import tempfile
import os
import base64
import logging
from collections import namedtuple
from runners.core import RunnerCaps, ZephyrBinaryRunner

# Import scripts from the current directory
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tt_boot_fs
import pcie_utils

# Set environment variable for protobuf implementation
if os.environ.get("PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION") != "python":
    os.environ["PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION"] = "python"
from encode_spirom_bins import convert_proto_txt_to_bin_file

try:
    from pyocd.core.helpers import ConnectHelper
    from pyocd.flash.file_programmer import FileProgrammer
except ImportError:
    print("Required module 'pyocd' not found. Please run 'pip install pyocd'.")
    sys.exit(1)

PYOCD_TARGET = "STM32G0B1CEUx"
BOARD_ID_MAP = {
    "p100": [
        {
            "upi": 0x36,
            "name": "P100-1",
            "protobuf_name": "P100",
            "pyocd_config": "pyocd_config.py",
        },
    ],
    "p100a": [
        {
            "upi": 0x43,
            "name": "P100A-1",
            "protobuf_name": "P100A",
            "pyocd_config": "pyocd_config.py",
        },
    ],
    "p150a": [
        {
            "upi": 0x40,
            "name": "P150A-1",
            "protobuf_name": "P150A",
            "pyocd_config": "pyocd_config.py",
        },
    ],
    "p150b": [
        {
            "upi": 0x41,
            "name": "P150B-1",
            "protobuf_name": "P150B",
            "pyocd_config": "pyocd_config.py",
        },
    ],
    "p150c": [
        {
            "upi": 0x42,
            "name": "P150C-1",
            "protobuf_name": "P150C",
            "pyocd_config": "pyocd_config.py",
        },
    ],
}

FlashOperation = namedtuple("FlashOperation", ["data", "pyocd_config"])

logger = logging.getLogger(__name__)


class TTBootStrapRunner(ZephyrBinaryRunner):
    """
    Runner to load firmware directly into the SPI flash of a PCIe card. This
    direct loading method does not require functional firmware on the card,
    so it will not readback the board ID. Instead, the runner will reprogram
    the ID in flash.
    """

    def __init__(
        self, cfg, board_id, board_name, fw_bundle, bootfs_hex, asic_id, adapter_id
    ):
        super().__init__(cfg)
        self.build_dir = Path(cfg.build_dir)
        self.board_dir = Path(cfg.board_dir)
        self.board_id = board_id
        self.board_name = board_name
        self.adapter_id = adapter_id
        self.pyocd_path = (
            Path(__file__).parent / "tooling/blackhole_recovery/data/bh_flm"
        )
        if self.board_name not in BOARD_ID_MAP:
            raise ValueError(
                f"Unknown board name: {self.board_name}. Supported names: "
                "{list(BOARD_ID_MAP.keys())}"
            )

        # For flashing a hex/bin file, we only will write to the eeprom for one ASIC.
        # If we parse a firmware bundle, we will update all ASICs
        pyocd_config = self.pyocd_path / Path(
            BOARD_ID_MAP[self.board_name][asic_id]["pyocd_config"]
        )

        # We support flashing hex or binary files directly. Otherwise, we will
        # extract the firmware from fwbundle
        if bootfs_hex:
            logger.info(
                "Hex file provided, using this directly (board ID argument ignored)"
            )
            # Load the hex file as data to write
            try:
                self.flash_data = [
                    FlashOperation(open(bootfs_hex, "rb").read(), pyocd_config)
                ]
            except FileNotFoundError as e:
                raise RuntimeError(f"Hex file {bootfs_hex} does not exist") from e
        else:
            if fw_bundle:
                self.flash_data = self.parse_fwbundle(fw_bundle)
            else:
                self.flash_data = self.parse_fwbundle(
                    str(Path(cfg.build_dir).parent / "update.fwbundle")
                )

    @classmethod
    def name(cls):
        return "tt_bootstrap"

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={"flash"}, file=True)

    @classmethod
    def do_add_parser(cls, parser):
        parser.add_argument(
            "--board-id", default=0, type=int, help="Serial number to assign to card"
        )
        parser.add_argument(
            "--board-name", type=str, help="Board name. Used to generate the board ID"
        )
        parser.add_argument("--bootfs-hex", type=str, help="Hex file to flash")
        parser.add_argument(
            "--fw-bundle", type=str, help="Path to the firmware bundle to flash"
        )
        parser.add_argument(
            "--asic-id",
            type=int,
            default=0,
            help="ASIC ID to flash when using hex files",
        )
        parser.add_argument(
            "--adapter-id",
            type=str,
            help="Adapter ID for the ST-Link device used in recovery",
        )

    @classmethod
    def do_create(cls, cfg, args):
        return cls(
            cfg,
            args.board_id,
            args.board_name,
            args.fw_bundle,
            args.bootfs_hex,
            args.asic_id,
            args.adapter_id,
        )

    def parse_fwbundle(self, fw_bundle):
        """
        Parses the firmware bundle and returns the data to flash.
        """
        board_cfg = BOARD_ID_MAP[self.board_name].copy()
        # Create a temporary directory to extract the tar file
        try:
            with (
                tempfile.TemporaryDirectory() as temp_dir,
                tarfile.open(fw_bundle, "r") as tar,
            ):
                tar.extractall(path=temp_dir)
                self.logger.debug(f"Extracted firmware bundle to {temp_dir}")
                # Now, load the firmware file as binary (converting back from base16)
                for cfg in board_cfg:
                    board_name = cfg["name"]
                    try:
                        with open(Path(temp_dir) / board_name / "image.bin", "r") as f:
                            data = base64.b16decode(f.read().strip())
                            cfg["bootfs"] = data
                    except FileNotFoundError as e:
                        raise RuntimeError(
                            f"Firmware file {board_name}/image.bin not found in bundle: {e}"
                        ) from e
        except tarfile.TarError as e:
            raise RuntimeError(f"Failed to extract firmware bundle: {e}") from e
        # Now, we need to set the board ID within the bootfs data
        build_folder = self.build_dir / "zephyr/python_proto_files"
        sys.path.append(str(build_folder))
        try:
            import read_only_pb2  # pylint: disable=import-outside-toplevel
        except ImportError as e:
            print(f"Error importing protobuf modules: {e}")
            print("Ensure the protobuf files are generated and the path is correct.")
            sys.exit(1)
        for cfg in board_cfg:
            board_name = cfg["protobuf_name"]
            if self.board_id == 0:
                # Autogenerate board ID
                board_id = cfg["upi"] << 36 | (0x1 << 32)
            else:
                # Use the provided board ID
                board_id = self.board_id
            convert_proto_txt_to_bin_file(
                self.board_dir / "spirom_data_tables",
                board_name,
                Path(temp_dir) / "generated_proto_bins",
                "read_only",
                read_only_pb2.ReadOnly,
                False,
                override={"board_id": board_id},
            )
            protobuf_bin = open(
                Path(temp_dir) / "generated_proto_bins" / board_name / "read_only.bin",
                "rb",
            ).read()
            # Load the blob as a bootfs, and set the board ID
            bootfs_entries = tt_boot_fs.BootFs.from_binary(cfg["bootfs"]).entries
            boardcfg = bootfs_entries["boardcfg"]
            # Recreate boardcfg entry with the protobuf data
            order = list(bootfs_entries.keys())
            order.remove("failover")
            failover = bootfs_entries["failover"]
            bootfs_entries["boardcfg"] = tt_boot_fs.FsEntry(
                False, "boardcfg", protobuf_bin, boardcfg.spi_addr, 0x0, False
            )
            cfg["bootfs"] = tt_boot_fs.BootFs(
                order, bootfs_entries, failover
            ).to_intel_hex()

        # Now, create FlashOperation objects for each ASIC on board
        operations = []
        for cfg in board_cfg:
            operations.append(
                FlashOperation(
                    cfg["bootfs"], self.pyocd_path / Path(cfg["pyocd_config"])
                )
            )
        return operations

    def do_run(self, command, **kwargs):
        """
        Execute a runner command. For this runner, we only support the 'flash' command.
        """
        if command != "flash":
            raise ValueError(f"Unsupported command: {command}")
        for flash_op in self.flash_data:
            if self.adapter_id is None:
                print(
                    "No adapter ID provided, please select the debugger "
                    "attached to STM32 if prompted"
                )
                session = ConnectHelper.session_with_chosen_probe(
                    target_override=PYOCD_TARGET,
                    user_script=flash_op.pyocd_config,
                )
            else:
                session = ConnectHelper.session_with_chosen_probe(
                    target_override=PYOCD_TARGET,
                    user_script=flash_op.pyocd_config,
                    unique_id=self.adapter_id,
                )
            session.open()
            target = session.board.target
            # Program the flash with the provided data
            with tempfile.TemporaryDirectory() as dir_name:
                temp_file_name = Path(dir_name) / "flash_data.hex"
                temp_file = open(temp_file_name, "wb")
                self.logger.info("Writing data to SPI flash")
                temp_file.write(flash_op.data)
                temp_file.close()
                # Use the FileProgrammer to write the data
                self.logger.debug(f"Programming from {temp_file_name}")
                FileProgrammer(session).program(str(temp_file_name))
            self.logger.debug("Resetting DMC after programming")
            target.reset_and_halt()
            target.resume()
            session.close()
            # Rescan PCIe bus
            self.logger.info(
                "Waiting up to 60 seconds for the DMC to complete the update"
            )
            timeout = 60  # Timeout in seconds
            start_time = time.time()
            while True:
                pcie_utils.rescan_pcie()
                if len(pcie_utils.find_tt_devs()) == len(self.flash_data):
                    # All devices found, exit loop and flash next ASIC
                    self.logger.info("Device reappeared after rescan")
                    break
                if time.time() - start_time > timeout:
                    self.logger.error("Timeout expired while waiting for PCIe rescan.")
                    break
                # Throttle rescan attempts
                time.sleep(1)
