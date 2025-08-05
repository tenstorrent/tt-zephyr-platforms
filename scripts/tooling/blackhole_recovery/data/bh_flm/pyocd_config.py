from pathlib import Path
from pyocd.target.pack.flash_algo import PackFlashAlgo

spiflash_base = 0x0
spiflash_size = 0x4000000  # 64 MB

def read_flash_memory(address, size) -> Sequence[int]:
    """
    Read a block of memory from the flash region.

    :param address: The starting address to read from.
    :param size: The number of bytes to read.
    :return: A bytearray containing the read data.
    """
    ap = next(iter(target.aps.values()))
    if address < spiflash_base or address + size > spiflash_base + spiflash_size:
       # Use the target's memory read function
       return ap.read_memory_original(address, size)
    # Call the custom read function within the FLM
    region = target.memory_map.get_region_for_address(address)
    if not region.flash._is_api_valid("pc_read"):
        raise RuntimeError(f"Flash read function not available for region at address {address:#x} with size {size:#x}.")
    pc_read = region.flash.flash_algo["pc_read"]
    region.flash.init(region.flash.Operation.VERIFY)
    # Read into device side ram buffer
    result = region.flash._call_function_and_wait(pc_read, r0=address,
            r1=size, r2=region.flash.begin_data, timeout=5.0)
    if result != 0:
        raise RuntimeError(f"Failed to read flash memory at address {address:#x} with size {size:#x}. Error code: {result}")
    # Copy from device RAM to sequence
    data = ap.read_memory_original(region.flash.begin_data, size)
    region.flash.cleanup()
    return data

class SPIPackFlashAlgo(PackFlashAlgo):
    """
    A custom subclass of the PackFlashAlgo class that is used to
    add a custom flash read function to the FLM
    """
    REQUIRED_SYMBOLS = {
        "Init",
        "UnInit",
        "EraseSector",
        "ProgramPage",
        "Read",
        }

    def get_pyocd_flash_algo(self, blocksize: int, ram_region: "RamRegion") -> Dict[str, Any]:
        """
        Returns the flash algorithm as a dictionary.
        """
        algo = super().get_pyocd_flash_algo(blocksize, ram_region)
        code_start = algo["load_address"] + self._FLASH_BLOB_HEADER_SIZE
        algo["pc_read"] = code_start + self.symbols["Read"]
        return algo


"""
Called by pyocd at target connection time
"""
def will_connect():
    flm = Path(__file__).parent / "STM32G0Bx_SPI_EEPROM.FLM"
    with flm.open("rb") as f:
        flash_algo = SPIPackFlashAlgo(f)

    # Define a fake flash region for the SPI NOR flash.
    spiflash = FlashRegion(
                name="External SPI EEPROM",
                start=spiflash_base,
                length=spiflash_size,
                blocksize=0x1000,
                page_size=0x100,
                flm=flash_algo,
                )

    # Add the spi flash region to the memory map.
    target.memory_map.add_region(spiflash)

    # This is a bit of a hack. PYOCD assumes that all programmable flash is
    # memory mapped, so we need to override the memory read function to
    # read from the SPI NOR flash instead of the default memory read function.

    # Save the original memory read function so we can call it later.
    target.read_memory_original = target.read_memory_block8
    # Manually override the memory read function to read from the SPI NOR flash.
    target.read_memory_block8 = read_flash_memory

def did_connect():
    # Go ahead and replace the AP read function too- we need this one overridden
    # as well, using the same hack as above
    ap = next(iter(target.aps.values()))
    ap.read_memory_original = ap.read_memory_block8
    ap.read_memory_block8 = read_flash_memory
