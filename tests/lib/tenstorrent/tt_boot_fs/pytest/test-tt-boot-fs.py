# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import base64
import logging
import pykwalify.core
import sys
import tarfile
import yaml

from pathlib import Path
from urllib.request import urlretrieve

TEST_ROOT = Path(__file__).parent.resolve()
MODULE_ROOT = TEST_ROOT.parents[4]

TEST_ALIGNMENT = 0x1000

sys.path.append(str(MODULE_ROOT / "scripts"))

import tt_boot_fs  # noqa: E402

try:
    from yaml import CSafeLoader as SafeLoader
except ImportError:
    from yaml import SafeLoader

logger = logging.getLogger(__name__)

_cached_test_image = None
_cached_released_image = None
_cached_corrupted_test_image = None


def _align_up(val, alignment):
    return (val + alignment - 1) & ~(alignment - 1)


# @pytest.fixture(scope="session") ?
def gen_test_image(tmp_path: Path):
    global _cached_test_image
    if _cached_test_image is None:
        # Note: we don't (yet) use base64 encoding in this bin file, it's just binary data
        pth = tmp_path / "image.bin"
        spi_addr = tt_boot_fs.IMAGE_ADDR

        with open(pth, "wb") as f:
            pad_byte = b"\xff"

            # define 2 images
            image_A = b"\x73\x73\x42\x42"
            fd_A = tt_boot_fs.FsEntry(
                tag="imageA",
                data=image_A,
                load_addr=0x1000000,
                executable=True,
                provisioning_only=False,
                spi_addr=spi_addr,
            )
            spi_addr += len(image_A)
            spi_addr = _align_up(spi_addr, TEST_ALIGNMENT)

            image_B = b"\x73\x73\x42\x42\x37\x37\x24\x24"
            fd_B = tt_boot_fs.FsEntry(
                tag="imageB",
                data=image_B,
                provisioning_only=False,
                spi_addr=spi_addr,
                load_addr=None,
                executable=False,
            )
            spi_addr += len(image_A)
            spi_addr = _align_up(spi_addr, TEST_ALIGNMENT)

            # define 1 recovery image
            image_C = b"\x73\x73\x42\x42"
            fd_C = tt_boot_fs.FsEntry(
                tag="failover",
                data=image_C,
                spi_addr=spi_addr,
                load_addr=0x1000000,
                executable=False,
                provisioning_only=False,
            )

            # manually assemble a tt_boot_fs
            fs = b""

            # append file descriptors A and B
            fs += fd_A.descriptor()
            fs += fd_B.descriptor()

            # pad to FAILOVER_HEAD_ADDR
            pad_size = tt_boot_fs.FAILOVER_HEAD_ADDR - len(fs)
            padding = pad_byte * pad_size
            fs += padding

            # append recovery file descriptor (C)
            fs += fd_C.descriptor()

            # pad to SPI_RX_ADDR
            pad_size = tt_boot_fs.SPI_RX_ADDR - len(fs)
            padding = pad_byte * pad_size
            fs += padding

            # write SPI RX training data
            fs += tt_boot_fs.SPI_RX_VALUE.to_bytes(
                tt_boot_fs.SPI_RX_SIZE, byteorder="little"
            )

            # append images
            fs += image_A
            offs = len(fs)
            offs = _align_up(offs, TEST_ALIGNMENT)
            pad_size = offs - len(fs)
            padding = pad_byte * pad_size
            fs += padding

            fs += image_B
            offs = len(fs)
            offs = _align_up(offs, TEST_ALIGNMENT)
            pad_size = offs - len(fs)
            padding = pad_byte * pad_size
            fs += padding

            fs += image_C
            offs = len(fs)
            offs = _align_up(offs, TEST_ALIGNMENT)
            pad_size = offs - len(fs)
            padding = pad_byte * pad_size
            fs += padding

            f.write(fs)
            _cached_test_image = (fs, pth)

    return _cached_test_image


def get_test_image_path(tmp_path: Path):
    _, pth = gen_test_image(tmp_path)
    return pth


def gen_released_image(tmp_path: Path):
    global _cached_released_image

    if _cached_released_image is None:
        # Test on a recent experimental release (note: stable release does not have a recovery image)
        URL = (
            "https://github.com/tenstorrent/tt-firmware/raw/"
            "7bc0a90226e684962fb039cf26580356d7646574"
            "/fw_pack-80.15.0.0.fwbundle"
        )
        targz = tmp_path / "fw_pack.tar.gz"
        urlretrieve(URL, targz)

        with tarfile.open(targz, "r") as tar:
            tar.extractall(tmp_path / "fw_pack")

        with open(tmp_path / "fw_pack" / "P100-1" / "image.bin", "r") as f:
            data = base64.b16decode(f.read())

        pth = tmp_path / "fw_pack" / "image.bin"

        with open(pth, "wb") as f:
            f.write(data)

        _cached_released_image = (data, pth)

    return _cached_released_image


def get_released_image_path(tmp_path: Path):
    _, pth = gen_released_image(tmp_path)
    return pth


def gen_corrupted_test_image(tmp_path: Path):
    global _cached_corrupted_test_image

    if _cached_corrupted_test_image is None:
        data, pth = gen_test_image(tmp_path)
        pth = Path(str(pth) + ".corrupted")

        data = bytearray(data)
        data[42] = 0x42
        data = bytes(data)

        with open(pth, "wb") as f:
            f.write(data)
            _cached_corrupted_test_image = (data, pth)

    return _cached_corrupted_test_image


def get_corrupted_test_image_path(tmp_path: Path):
    _, pth = gen_corrupted_test_image(tmp_path)
    return pth


def test_tt_boot_fs_schema():
    SCHEMA_PATH = MODULE_ROOT / "scripts" / "schemas" / "tt-boot-fs-schema.yml"
    SPEC_PATH = TEST_ROOT / "p100.yml"

    schema = None
    spec = None
    with open(SCHEMA_PATH) as f:
        schema = yaml.load(f, Loader=SafeLoader)
    with open(SPEC_PATH) as f:
        spec = yaml.load(f, Loader=SafeLoader)
    spec = pykwalify.core.Core(source_data=spec, schema_data=schema).validate()


def test_tt_boot_fs_mkfs():
    """
    Test the ability to make a tt_boot_fs.
    """
    assert (
        tt_boot_fs.mkfs(TEST_ROOT / "p100.yml") is not None
    ), "tt_boot_fs.mkfs() failed"


def test_tt_boot_fs_fsck(tmp_path: Path):
    """
    Test the ability to check a tt_boot_fs.
    """
    assert tt_boot_fs.fsck(
        get_test_image_path(tmp_path)
    ), "tt_boot_fs.fsck() failed with valid image"
    assert not tt_boot_fs.fsck(
        get_corrupted_test_image_path(tmp_path)
    ), "tt_boot_fs.fsck() succeeded with invalid image"

    assert tt_boot_fs.fsck(
        get_released_image_path(tmp_path)
    ), "tt_boot_fs.fsck() failed with released image"


def test_tt_boot_fs_cksum():
    """
    Test the ability to generate a correct tt_boot_fs checksum.

    This test is intentionally consistent with the accompanying C ZTest in src/main.c.
    """

    items = [
        (0, []),
        (0, b"\x42"),
        (0x42427373, b"\x73\x73\x42\x42"),
        (0x6666AAAA, b"\x73\x73\x42\x42\x37\x37\x24\x24"),
    ]

    for it in items:
        assert tt_boot_fs.cksum(it[1]) == it[0]
