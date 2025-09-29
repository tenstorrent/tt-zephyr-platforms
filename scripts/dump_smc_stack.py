#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""
This script dumps SMC firmware task stack data using symbols from the ELF file.
It is intended to aid in debugging firmware issues by examining stack contents
and usage. The script requires that the SMC be accessible over PCIe and a
zephyr.elf file with debug symbols.
"""

import argparse
import errno
import struct

try:
    import pyluwen
    from pcie_utils import rescan_pcie

    HAS_PYLUWEN = True
except ImportError:
    HAS_PYLUWEN = False

try:
    from elftools.elf.elffile import ELFFile
    from elftools.elf.sections import SymbolTableSection

    HAS_ELFTOOLS = True
except ImportError:
    HAS_ELFTOOLS = False

# Default stacks to dump
DEFAULT_STACKS = [
    "shell_uart_stack",
    "logging_stack",
    "sys_work_q_stack",
    "z_main_stack",
]

# Stack analysis constants
STACK_FILL_PATTERN = 0xAA  # Zephyr's default stack fill pattern
BYTES_PER_LINE = 16
WORD_SIZE = 4  # 32-bit words

# TCB structure offsets for different architectures
# These offsets are based on empirical findings from the SMC firmware
TCB_STACK_POINTER_OFFSET = 0x28  # Current stack pointer location in TCB

# Historical/alternative offsets for reference
TCB_ARCH_OFFSETS = {
    "arm": {
        "callee_saved": 0,  # First field is usually callee_saved context
        "psp_offset": 13 * 4,  # PSP is typically at offset 13 in ARM context (52 bytes)
        "current_sp": 0x28,  # Empirically found current SP offset
    },
    "generic": {
        "context_offset": 0,  # Try various offsets in the context structure
        "possible_sp_offsets": [
            0,
            4,
            8,
            12,
            16,
            20,
            0x28,
            48,
            52,
            56,
            60,
            64,
        ],  # Common SP locations
    },
}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Dump SMC firmware task stacks for debugging.",
        allow_abbrev=False,
        epilog="""
Examples:
  %(prog)s zephyr.elf                                    # Dump default stacks (used portion only for analysis)
  %(prog)s zephyr.elf --asic-id 1                        # Dump from ASIC 1
  %(prog)s zephyr.elf --analyze-callstack                # Analyze and show function calls in stack
  %(prog)s zephyr.elf --analyze-callstack --hex-dump     # Full analysis with annotated hex dump
  %(prog)s zephyr.elf --debug-tcb                        # Show TCB contents to find stack pointer
  %(prog)s zephyr.elf --list-stacks                      # List all available stacks
  %(prog)s zephyr.elf --stacks my_stack                  # Dump specific stack
        """,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "elf_file", type=str, help="Path to zephyr.elf file with debug symbols"
    )
    parser.add_argument(
        "--asic-id",
        type=int,
        default=0,
        help="Specify which ASIC to dump stacks from (default: 0)",
    )
    parser.add_argument(
        "--stacks",
        type=str,
        nargs="+",
        default=DEFAULT_STACKS,
        help="Specify which stacks to dump (default: shell_uart_stack, logging_stack, sys_work_q_stack)",
    )
    parser.add_argument(
        "--hex-dump", action="store_true", help="Show hex dump of stack contents"
    )
    parser.add_argument(
        "--list-stacks",
        action="store_true",
        help="List all available stack symbols in the ELF file",
    )
    parser.add_argument(
        "--analyze-callstack",
        action="store_true",
        help="Analyze and pretty print function calls found in stack",
    )
    parser.add_argument(
        "--debug-tcb",
        action="store_true",
        help="Show TCB contents for debugging stack pointer location",
    )
    return parser.parse_args()


def build_symbol_table(elf_file_path):
    """
    Build a symbol table for address-to-name resolution
    Returns a sorted list of (address, size, name, type) tuples
    """
    if not HAS_ELFTOOLS:
        return None

    symbols = []

    try:
        with open(elf_file_path, "rb") as f:
            elf = ELFFile(f)

            # Look through all symbol tables
            for section in elf.iter_sections():
                if isinstance(section, SymbolTableSection):
                    for symbol in section.iter_symbols():
                        symbol_name = symbol.name
                        if not symbol_name:
                            continue

                        # Get symbol type
                        sym_type = symbol["st_info"]["type"]

                        # We're interested in functions and objects
                        if sym_type in ["STT_FUNC", "STT_OBJECT", "STT_NOTYPE"]:
                            symbols.append(
                                (
                                    symbol["st_value"],  # address
                                    symbol["st_size"],  # size
                                    symbol_name,  # name
                                    sym_type,  # type
                                )
                            )
    except Exception as e:
        print(f"Error reading ELF file for symbols: {e}")
        return None

    # Sort by address for efficient lookup
    symbols.sort(key=lambda x: x[0])
    return symbols


def find_stack_symbols(elf_file_path, stack_names=None):
    """
    Parse ELF file to find stack symbol addresses and sizes
    If stack_names is None, return all symbols that look like stacks
    """
    if not HAS_ELFTOOLS:
        print("Error: pyelftools library is required but not available")
        print("Install with: pip install pyelftools")
        return None

    stacks = {}

    try:
        with open(elf_file_path, "rb") as f:
            elf = ELFFile(f)

            # Look through all symbol tables
            for section in elf.iter_sections():
                if isinstance(section, SymbolTableSection):
                    for symbol in section.iter_symbols():
                        symbol_name = symbol.name
                        if not symbol_name:
                            continue

                        # If specific stacks requested, look for those
                        if stack_names:
                            if symbol_name in stack_names:
                                # Validate that the symbol has a reasonable size
                                if symbol["st_size"] > 0:
                                    stacks[symbol_name] = {
                                        "address": symbol["st_value"],
                                        "size": symbol["st_size"],
                                    }
                                else:
                                    print(
                                        f"Warning: Stack symbol '{symbol_name}' has zero size"
                                    )
                        else:
                            # Look for all stack-like symbols
                            if "stack" in symbol_name.lower() and symbol["st_size"] > 0:
                                stacks[symbol_name] = {
                                    "address": symbol["st_value"],
                                    "size": symbol["st_size"],
                                }
    except Exception as e:
        print(f"Error reading ELF file {elf_file_path}: {e}")
        return None

    return stacks


def find_thread_tcbs(elf_file_path, stack_names):
    """
    Find Thread Control Blocks (TCBs) associated with the given stacks
    Returns a dict mapping stack names to TCB info
    """
    if not HAS_ELFTOOLS:
        return {}

    tcbs = {}

    try:
        with open(elf_file_path, "rb") as f:
            elf = ELFFile(f)

            # Look for thread symbols that might be associated with stacks
            for section in elf.iter_sections():
                if isinstance(section, SymbolTableSection):
                    for symbol in section.iter_symbols():
                        symbol_name = symbol.name
                        if not symbol_name:
                            continue

                        # Look for thread symbols that match our stack names
                        # Common patterns: "thread_<name>", "<name>_thread", "_k_thread_obj_<name>"
                        for stack_name in stack_names:
                            # Remove "_stack" suffix to get base name
                            base_name = stack_name.replace("_stack", "").replace(
                                "Stack", ""
                            )

                            # Check various thread naming patterns
                            if (
                                symbol_name == f"{base_name}_thread"
                                or symbol_name == f"thread_{base_name}"
                                or symbol_name == f"_k_thread_obj_{base_name}"
                                or symbol_name == f"{base_name}_thread_data"
                                or symbol_name == f"k_{base_name}"
                                or base_name in symbol_name
                                and "thread" in symbol_name.lower()
                            ):
                                tcbs[stack_name] = {
                                    "tcb_address": symbol["st_value"],
                                    "tcb_size": symbol["st_size"],
                                    "tcb_symbol": symbol_name,
                                }
                                break

    except Exception as e:
        print(f"Error finding TCBs: {e}")

    return tcbs


def calculate_stack_usage_from_tcb(stack_base, stack_size, stack_ptr):
    """
    Calculate actual stack usage given stack pointer from TCB
    Returns (used_bytes, actual_stack_top) or None if invalid
    """
    if not stack_ptr:
        return None, None

    # Zephyr stacks grow downward from (base + size) toward base
    stack_top = stack_base + stack_size

    # Validate stack pointer is within bounds
    if stack_base <= stack_ptr <= stack_top:
        used_bytes = stack_top - stack_ptr
        return used_bytes, stack_ptr
    else:
        print(
            f"Warning: Stack pointer 0x{stack_ptr:08x} outside stack bounds [0x{stack_base:08x}, 0x{stack_top:08x}]"
        )
        return None, None


def resolve_address(address, symbol_table):
    """
    Resolve an address to a symbol name using binary search
    Returns (symbol_name, offset) or (None, 0) if not found
    """
    if not symbol_table:
        return None, 0

    # Binary search for the symbol containing this address
    left, right = 0, len(symbol_table) - 1
    best_match = None

    while left <= right:
        mid = (left + right) // 2
        sym_addr, sym_size, sym_name, sym_type = symbol_table[mid]

        if sym_addr <= address < sym_addr + sym_size:
            # Perfect match - address is within this symbol
            return sym_name, address - sym_addr
        elif sym_addr <= address:
            # This symbol starts before our address, could be a match
            best_match = (sym_name, address - sym_addr, sym_type)
            left = mid + 1
        else:
            right = mid - 1

    # If we have a best match and the offset is reasonable (< 64KB)
    if best_match and best_match[1] < 65536:
        return best_match[0], best_match[1]

    return None, 0


def debug_tcb_contents(chip, tcb_address, tcb_size, stack_base, stack_size):
    """
    Debug function to show TCB contents for manual analysis
    """
    print(f"    TCB Debug - Address: 0x{tcb_address:08x}, Size: {tcb_size}")
    print(f"    Stack range: 0x{stack_base:08x} - 0x{stack_base + stack_size:08x}")

    try:
        max_tcb_read = min(tcb_size, 128) if tcb_size > 0 else 64

        print(f"    TCB contents (first {max_tcb_read} bytes):")
        for offset in range(0, max_tcb_read, 16):
            line_data = []
            line_hex = []

            for i in range(16):
                if offset + i < max_tcb_read:
                    try:
                        if i % 4 == 0 and offset + i + 3 < max_tcb_read:
                            # Read as 32-bit word
                            word_addr = tcb_address + offset + i
                            word_val = chip.axi_read32(word_addr)
                            line_data.append(word_val)
                            line_hex.extend(
                                [(word_val >> (8 * j)) & 0xFF for j in range(4)]
                            )
                        else:
                            line_hex.append(0)  # Placeholder
                    except Exception:
                        line_hex.append(0)
                else:
                    line_hex.append(0)

            # Format hex output
            hex_str = " ".join(f"{b:02x}" for b in line_hex[:16])

            # Show 32-bit values with stack range annotations
            annotations = []
            for i, word_val in enumerate(line_data):
                word_offset = offset + (i * 4)
                if stack_base <= word_val <= stack_base + stack_size:
                    annotations.append(f"[+{word_offset:02x}]=SP?")

            annotation_str = " ".join(annotations) if annotations else ""
            print(f"      +{offset:02x}: {hex_str:<48} {annotation_str}")

    except Exception as e:
        print(f"    Error reading TCB: {e}")


def get_stack_pointer_from_tcb(chip, tcb_address, stack_base, stack_size):
    """
    Get the current stack pointer from TCB at the known offset +0x28
    Returns stack pointer value or None if invalid
    """
    try:
        # Stack pointer is at TCB + 0x28 based on empirical findings
        sp_offset = TCB_STACK_POINTER_OFFSET
        stack_ptr = chip.axi_read32(tcb_address + sp_offset)

        # Validate that this looks like a reasonable stack pointer
        stack_top = stack_base + stack_size
        if stack_base <= stack_ptr <= stack_top:
            return stack_ptr, sp_offset
        else:
            print(
                f"    Warning: SP at TCB+{sp_offset:02x} (0x{stack_ptr:08x}) outside stack range [0x{stack_base:08x}, 0x{stack_top:08x}]"
            )
            return None, None

    except Exception as e:
        print(f"    Error reading stack pointer from TCB+{28:02x}: {e}")
        return None, None


def analyze_tcb_for_stack_pointer(
    chip, tcb_address, tcb_size, stack_base, stack_size, debug=False
):
    """
    Try to find the stack pointer in the TCB by analyzing potential addresses
    First tries the known offset +28, then falls back to searching if that fails
    Returns the most likely stack pointer or None
    """
    if debug:
        debug_tcb_contents(chip, tcb_address, tcb_size, stack_base, stack_size)

    # First, try the known offset +28
    stack_ptr, sp_offset = get_stack_pointer_from_tcb(
        chip, tcb_address, stack_base, stack_size
    )
    if stack_ptr is not None:
        return stack_ptr, sp_offset

    # If that failed, fall back to searching through the TCB
    print("    Known offset failed, searching TCB for stack pointer...")
    candidates = []

    try:
        # Read the entire TCB structure (up to a reasonable limit)
        max_tcb_read = min(tcb_size, 256) if tcb_size > 0 else 64

        for offset in range(0, max_tcb_read, 4):
            try:
                word_addr = tcb_address + offset
                potential_sp = chip.axi_read32(word_addr)

                # Check if this looks like a valid stack pointer
                stack_top = stack_base + stack_size
                if stack_base <= potential_sp <= stack_top:
                    # This is within our stack range - good candidate
                    score = 10  # High score for being in range
                    # Give extra points to the known offset in case we missed it above
                    if offset == TCB_STACK_POINTER_OFFSET:
                        score = 15
                    candidates.append(
                        {"offset": offset, "value": potential_sp, "score": score}
                    )
                elif (potential_sp & 0xFFFF0000) == (stack_base & 0xFFFF0000):
                    # Same memory region - medium score
                    candidates.append(
                        {"offset": offset, "value": potential_sp, "score": 5}
                    )

            except Exception:
                continue  # Skip invalid reads

    except Exception as e:
        print(f"    Error analyzing TCB: {e}")
        return None, None

    # Sort candidates by score and return the best one
    if candidates:
        best = max(candidates, key=lambda x: x["score"])
        print(
            f"    Found potential SP at TCB+{best['offset']:02x}: 0x{best['value']:08x} (score: {best['score']})"
        )
        return best["value"], best["offset"]

    return None, None


def list_available_stacks(elf_file_path):
    """
    List all available stack symbols in the ELF file
    """
    print(f"Scanning {elf_file_path} for stack symbols...")
    stacks = find_stack_symbols(elf_file_path, None)  # Get all stack-like symbols

    if not stacks:
        print("No stack symbols found in ELF file")
        return

    print(f"\nFound {len(stacks)} stack symbol(s):")
    for name, info in sorted(stacks.items()):
        print(f"  {name:<30} @ 0x{info['address']:08x} ({info['size']} bytes)")

    print(
        f"\nTotal stack memory: {sum(info['size'] for info in stacks.values())} bytes"
    )


def analyze_stack_usage(stack_data, stack_size):
    """
    Analyze stack usage by looking for the fill pattern
    """
    unused_bytes = 0

    # Count unused bytes from the bottom (filled with pattern)
    for byte in stack_data:
        if byte == STACK_FILL_PATTERN:
            unused_bytes += 1
        else:
            break

    used_bytes = stack_size - unused_bytes
    usage_percent = (used_bytes / stack_size) * 100 if stack_size > 0 else 0

    return {
        "total_size": stack_size,
        "used_bytes": used_bytes,
        "free_bytes": unused_bytes,
        "usage_percent": usage_percent,
    }


def analyze_callstack(stack_data, base_address, symbol_table):
    """
    Analyze stack data for potential function pointers and return addresses
    """
    if not symbol_table:
        return []

    callstack = []

    # Look through stack data in 4-byte (word) chunks
    for i in range(0, len(stack_data) - 3, WORD_SIZE):
        # Extract 32-bit value (little-endian)
        word_bytes = stack_data[i : i + 4]
        if len(word_bytes) == 4:
            potential_addr = struct.unpack("<I", word_bytes)[0]

            # Check if this looks like a valid code address
            # Typically code is in lower memory ranges
            if (
                0x1000000 <= potential_addr <= 0x10100000
            ):  # Reasonable code address range
                symbol_name, offset = resolve_address(potential_addr, symbol_table)
                if symbol_name:
                    stack_offset = base_address + i
                    callstack.append(
                        {
                            "stack_addr": stack_offset,
                            "target_addr": potential_addr,
                            "symbol": symbol_name,
                            "offset": offset,
                        }
                    )

    return callstack


def hex_dump_data(data, base_address, symbol_table=None):
    """
    Format data as a hex dump, optionally with symbol resolution
    """
    lines = []
    for i in range(0, len(data), BYTES_PER_LINE):
        chunk = data[i : i + BYTES_PER_LINE]
        addr = base_address + i
        hex_bytes = " ".join(f"{b:02x}" for b in chunk)
        ascii_chars = "".join(chr(b) if 32 <= b <= 126 else "." for b in chunk)
        line = f"  {addr:08x}: {hex_bytes:<48} |{ascii_chars}|"

        # If we have symbols, check for potential addresses in this line
        if symbol_table and len(chunk) >= 4:
            # Check each 4-byte alignment in this line
            for j in range(0, len(chunk) - 3, 4):
                if i + j + 4 <= len(data):
                    word_bytes = chunk[j : j + 4]
                    potential_addr = struct.unpack("<I", word_bytes)[0]
                    if 0x1000 <= potential_addr <= 0x10000000:
                        symbol_name, offset = resolve_address(
                            potential_addr, symbol_table
                        )
                        if symbol_name:
                            if offset == 0:
                                line += f"  <- {symbol_name}"
                            else:
                                line += f"  <- {symbol_name}+0x{offset:x}"
                            break  # Only show one symbol per line

        lines.append(line)
    return "\n".join(lines)


def dump_stack(
    chip,
    stack_name,
    stack_info,
    tcb_info=None,
    hex_dump=False,
    analyze_callstack_flag=False,
    symbol_table=None,
    debug_tcb=False,
):
    """
    Dump a single stack's contents and analysis
    """
    address = stack_info["address"]
    size = stack_info["size"]

    print(f"\n=== {stack_name} ===")
    print(f"Address: 0x{address:08x}")
    print(f"Size: {size} bytes")

    # Try to get actual stack usage from TCB if available (for analysis only)
    actual_used_bytes = None
    stack_ptr = None
    analysis_start = address  # For callstack analysis
    analysis_size = size  # For callstack analysis

    if tcb_info:
        print(f"TCB: {tcb_info['tcb_symbol']} @ 0x{tcb_info['tcb_address']:08x}")
        stack_ptr, sp_offset = analyze_tcb_for_stack_pointer(
            chip,
            tcb_info["tcb_address"],
            tcb_info["tcb_size"],
            address,
            size,
            debug_tcb,
        )

        if stack_ptr:
            print(f"Stack Pointer: 0x{stack_ptr:08x} (found at TCB+0x{sp_offset:02x})")
            # Calculate used portion (Zephyr stacks grow downward)
            stack_top = address + size
            if address <= stack_ptr <= stack_top:
                actual_used_bytes = stack_top - stack_ptr
                print(f"Current Usage: {actual_used_bytes} bytes (from TCB)")
                # Update analysis parameters to focus on used portion
                analysis_start = stack_ptr
                analysis_size = actual_used_bytes
                # Round up to word boundary for reading
                analysis_size = ((analysis_size + 3) // 4) * 4
        else:
            print("Could not find valid stack pointer in TCB")

    try:
        # Always read the full stack for hex dump
        full_stack_data = bytearray()
        for offset in range(0, size, 4):
            word_addr = address + offset
            word_data = chip.axi_read32(word_addr)
            # Convert 32-bit word to 4 bytes (little-endian)
            full_stack_data.extend(struct.pack("<I", word_data))

        # Trim to exact size
        full_stack_data = full_stack_data[:size]

        # For callstack analysis, use either TCB-limited or full data
        analysis_data = bytearray()
        if actual_used_bytes is not None and analyze_callstack_flag:
            # Read only the used portion for analysis
            for offset in range(0, analysis_size, 4):
                word_addr = analysis_start + offset
                word_data = chip.axi_read32(word_addr)
                analysis_data.extend(struct.pack("<I", word_data))
            analysis_data = analysis_data[:actual_used_bytes]
        else:
            # Use full stack for analysis
            analysis_data = full_stack_data
            analysis_start = address

        if analyze_callstack_flag and symbol_table:
            callstack = analyze_callstack(analysis_data, analysis_start, symbol_table)
            if callstack:
                print("Potential call stack:")
                for entry in callstack:
                    if entry["offset"] == 0:
                        print(
                            f"  [0x{entry['stack_addr']:08x}] -> {entry['symbol']} (0x{entry['target_addr']:08x})"
                        )
                    else:
                        print(
                            f"  [0x{entry['stack_addr']:08x}] -> {entry['symbol']}+0x{entry['offset']:x} (0x{entry['target_addr']:08x})"
                        )
            else:
                print("No recognizable function addresses found in stack")

        if hex_dump:
            print(f"Stack contents (full stack, {len(full_stack_data)} bytes):")
            print(
                hex_dump_data(
                    full_stack_data,
                    address,
                    symbol_table if analyze_callstack_flag else None,
                )
            )

    except Exception as e:
        print(f"Error reading stack data: {e}")


def dump_stacks(
    asic_id,
    elf_file_path,
    stack_names,
    hex_dump=False,
    analyze_callstack_flag=False,
    debug_tcb=False,
):
    """
    Dump specified firmware stacks from given ASIC
    """
    if not HAS_PYLUWEN:
        print("Error: pyluwen library is required but not available")
        print("Make sure pyluwen is installed and available in your Python environment")
        return errno.ENOENT

    # Parse ELF file for stack symbols
    print(f"Loading symbols from {elf_file_path}...")
    stacks = find_stack_symbols(elf_file_path, stack_names)

    if not stacks:
        print("No stack symbols found in ELF file")
        return errno.ENOENT

    missing_stacks = set(stack_names) - set(stacks.keys())
    if missing_stacks:
        print(
            f"Warning: Could not find symbols for stacks: {', '.join(missing_stacks)}"
        )
        print(
            "Available stack-like symbols in ELF might include variations of these names."
        )

    # Find TCBs for accurate stack usage
    print("Looking for Thread Control Blocks (TCBs)...")
    tcbs = find_thread_tcbs(elf_file_path, list(stacks.keys()))
    if tcbs:
        print(f"Found TCBs for: {', '.join(tcbs.keys())}")
    else:
        print("No TCBs found - will use fill pattern analysis")

    # Build symbol table for callstack analysis if requested
    symbol_table = None
    if analyze_callstack_flag:
        print("Building symbol table for callstack analysis...")
        symbol_table = build_symbol_table(elf_file_path)
        if symbol_table:
            print(f"Loaded {len(symbol_table)} symbols")
        else:
            print("Warning: Could not build symbol table")

    # Connect to chip
    print(f"Connecting to ASIC {asic_id}...")
    try:
        chip = pyluwen.PciChip(asic_id)
        # Test connectivity
        test_read = chip.axi_read32(0x80030060)  # SMC postcode register
        if test_read == 0xFFFFFFFF:
            print("SMC appears to need a pcie reset to be accessible")
            rescan_pcie()
            chip = pyluwen.PciChip(asic_id)
    except Exception as e:
        print(f"Error accessing SMC ASIC {asic_id}: {e}")
        print("Make sure the SMC is powered on and accessible over PCIe")
        return errno.EIO

    # Dump each stack
    total_stacks = len(stacks)
    total_size = sum(info["size"] for info in stacks.values())

    for stack_name, stack_info in stacks.items():
        tcb_info = tcbs.get(stack_name)  # Get TCB info if available
        dump_stack(
            chip,
            stack_name,
            stack_info,
            tcb_info,
            hex_dump,
            analyze_callstack_flag,
            symbol_table,
            debug_tcb,
        )

    # Summary
    if total_stacks > 0:
        print("\n=== Summary ===")
        print(f"Found and dumped {total_stacks} stack(s)")
        print(f"Total stack memory: {total_size} bytes")
        if analyze_callstack_flag and symbol_table:
            print("Callstack analysis was performed using symbol table")

    return 0


def main():
    """
    Main function to dump SMC firmware stacks
    """
    args = parse_args()

    # If just listing stacks, do that and exit
    if args.list_stacks:
        list_available_stacks(args.elf_file)
        return 0

    return dump_stacks(
        args.asic_id,
        args.elf_file,
        args.stacks,
        args.hex_dump,
        args.analyze_callstack,
        args.debug_tcb,
    )


if __name__ == "__main__":
    exit(main())
