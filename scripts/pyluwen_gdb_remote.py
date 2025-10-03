#!/usr/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""
GDB Remote Server for SMC debugging using pyluwen.

This script implements a GDB remote serial protocol server that uses pyluwen
to access SMC memory over PCIe. This allows GDB to debug SMC firmware remotely.

Usage:
    python3 pyluwen_gdb_remote.py [--port 2159] [--asic-id 0]

Then in GDB:
    (gdb) target remote localhost:2159
    etc.
"""

import argparse
import socket
import struct
import sys
from typing import Optional, Dict, Any

try:
    import pyluwen
except ImportError:
    print("Error: pyluwen not found. Make sure it's installed and accessible.")
    sys.exit(1)

from pcie_utils import rescan_pcie


# ARC_REGISTERS stubbed: all registers mapped to 0 (no real hardware access)
ARC_REGISTERS = {
    "r0": 0, "r1": 0, "r2": 0, "r3": 0, "r4": 0, "r5": 0, "r6": 0, "r7": 0,
    "r8": 0, "r9": 0, "r10": 0, "r11": 0, "r12": 0, "r13": 0, "r14": 0, "r15": 0,
    "r16": 0, "r17": 0, "r18": 0, "r19": 0, "r20": 0, "r21": 0, "r22": 0, "r23": 0,
    "r24": 0, "r25": 0, "gp": 0, "fp": 0, "sp": 0, "ilink1": 0, "ilink2": 0, "blink": 0,
    "pc": 0, "status32": 0, "lp_count": 0, "lp_start": 0, "lp_end": 0, "identity": 0, "debug": 0
}


class GDBRemoteServer:
    """GDB Remote Protocol Server using pyluwen for SMC access"""

    def __init__(self, asic_id: int = 0, port: int = 2159):
        self.asic_id = asic_id
        self.port = port
        self.chip: Optional[Any] = None
        self.socket: Optional[socket.socket] = None
        self.client_socket: Optional[socket.socket] = None
        self.running = False
        self.connected = False

        # GDB state
        self.last_signal = 5  # SIGTRAP
        self.breakpoints: Dict[int, bytes] = {}  # address -> original instruction

    def connect_chip(self) -> bool:
        """Connect to the SMC chip using pyluwen (stubbed, no register checks)"""
        try:
            print(f"Attempting to connect to SMC ASIC {self.asic_id}...")
            self.chip = pyluwen.PciChip(self.asic_id)
            print(f"Connected to SMC ASIC {self.asic_id}")
            return True
        except Exception as e:
            print(f"Error connecting to SMC ASIC {self.asic_id}: {e}")
            print("Make sure the SMC is powered on and accessible over PCIe")
            self.chip = None
            return False

    def start_server(self):
        """Start the GDB remote server"""
        if not self.connect_chip():
            return False

        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.socket.bind(("localhost", self.port))
            self.socket.listen(1)

            print(f"GDB Remote Server listening on port {self.port}")
            print(f"Connect with: (gdb) target remote localhost:{self.port}")

            self.running = True
            return True

        except Exception as e:
            print(f"Failed to start server: {e}")
            return False

    def wait_for_connection(self):
        """Wait for GDB to connect"""
        try:
            self.client_socket, addr = self.socket.accept()
            print(f"GDB connected from {addr}")
            self.connected = True
            return True
        except Exception as e:
            print(f"Error accepting connection: {e}")
            return False

    def stop_server(self):
        """Stop the server and cleanup"""
        self.running = False
        self.connected = False

        if self.client_socket:
            self.client_socket.close()
            self.client_socket = None

        if self.socket:
            self.socket.close()
            self.socket = None

    def read_memory(self, address: int, length: int) -> bytes:
        """Read memory from SMC using pyluwen"""
        if not self.chip:
            return b"\x00" * length

        try:
            data = bytearray()
            bytes_read = 0

            while bytes_read < length:
                # Read in 4-byte aligned chunks
                word_addr = (address + bytes_read) & 0xFFFFFFFC
                word = self.chip.axi_read32(word_addr)

                # Convert to bytes (little endian)
                word_bytes = struct.pack("<I", word)

                # Calculate which bytes we need from this word
                start_offset = (address + bytes_read) - word_addr
                bytes_needed = min(4 - start_offset, length - bytes_read)

                data.extend(word_bytes[start_offset : start_offset + bytes_needed])
                bytes_read += bytes_needed

            return bytes(data)

        except Exception as e:
            print(f"Error reading memory at 0x{address:08x}: {e}")
            return b"\x00" * length

    def write_memory(self, address: int, data: bytes) -> bool:
        """Write memory to SMC using pyluwen"""
        if not self.chip:
            return False

        try:
            # Note: This is a simplified implementation
            # Real SMC may have read-only regions or require special handling
            offset = 0
            while offset < len(data):
                # For now, we can only write full 32-bit words
                # This is a limitation that may need addressing for breakpoints
                word_addr = (address + offset) & 0xFFFFFFFC

                if offset + 4 <= len(data):
                    # Write full word
                    word_data = data[offset : offset + 4]
                    word = struct.unpack("<I", word_data)[0]
                    # Note: pyluwen may not have axi_write32, this might need adjustment
                    if hasattr(self.chip, "axi_write32"):
                        self.chip.axi_write32(word_addr, word)
                    else:
                        print(f"Warning: Write not supported at 0x{word_addr:08x}")
                        return False
                    offset += 4
                else:
                    # Partial word write - need to read-modify-write
                    current_word = self.chip.axi_read32(word_addr)
                    current_bytes = struct.pack("<I", current_word)

                    # Modify the bytes we want to change
                    start_offset = (address + offset) - word_addr
                    bytes_to_write = min(4 - start_offset, len(data) - offset)

                    new_bytes = bytearray(current_bytes)
                    new_bytes[start_offset : start_offset + bytes_to_write] = data[
                        offset : offset + bytes_to_write
                    ]

                    new_word = struct.unpack("<I", bytes(new_bytes))[0]
                    if hasattr(self.chip, "axi_write32"):
                        self.chip.axi_write32(word_addr, new_word)
                    else:
                        print(f"Warning: Write not supported at 0x{word_addr:08x}")
                        return False

                    offset += bytes_to_write

            return True

        except Exception as e:
            print(f"Error writing memory at 0x{address:08x}: {e}")
            return False

    def read_register(self, reg_name: str) -> int:
        """Stub: Always return 0 for all registers (no hardware access)"""
        return 0

    def calculate_checksum(self, data: str) -> str:
        """Calculate GDB remote protocol checksum"""
        checksum = sum(ord(c) for c in data) & 0xFF
        return f"{checksum:02x}"

    def send_packet(self, data: str):
        """Send a GDB remote protocol packet"""
        if not self.client_socket:
            return

        checksum = self.calculate_checksum(data)
        packet = f"${data}#{checksum}"

        try:
            self.client_socket.send(packet.encode("ascii"))
        except Exception as e:
            print(f"Error sending packet: {e}")

    def receive_packet(self) -> Optional[str]:
        """Receive a GDB remote protocol packet"""
        if not self.client_socket:
            return None

        try:
            # Look for start of packet
            while True:
                data = self.client_socket.recv(1)
                if not data:
                    return None
                if data == b"$":
                    break
                elif data == b"+":  # ACK
                    continue
                elif data == b"-":  # NACK
                    continue
                elif data == b"\x03":  # Ctrl+C (interrupt)
                    return "interrupt"

            # Read until # (checksum marker)
            packet_data = ""
            while True:
                data = self.client_socket.recv(1)
                if not data:
                    return None
                if data == b"#":
                    break
                packet_data += data.decode("ascii")

            # Read checksum
            checksum_data = self.client_socket.recv(2)
            if not checksum_data:
                return None

            # Verify checksum
            expected_checksum = self.calculate_checksum(packet_data)
            received_checksum = checksum_data.decode("ascii")

            if expected_checksum.lower() == received_checksum.lower():
                # Send ACK
                self.client_socket.send(b"+")
                return packet_data
            else:
                # Send NACK
                self.client_socket.send(b"-")
                print(
                    f"Checksum mismatch: expected {expected_checksum}, got {received_checksum}"
                )
                return None

        except Exception as e:
            print(f"Error receiving packet: {e}")
            return None

    def handle_query(self, query: str) -> str:
        """Handle GDB query commands"""
        if query.startswith("qSupported"):
            # Advertise our capabilities
            return "PacketSize=4000;qXfer:features:read+"
        elif query == "qC":
            # Current thread ID (we only have one)
            return "QC1"
        elif query == "qfThreadInfo":
            # First thread info
            return "m1"
        elif query == "qsThreadInfo":
            # Subsequent thread info (none)
            return "l"
        elif query.startswith("qXfer:features:read:target.xml"):
            # Target description - programmatically generated from ARC_REGISTERS
            reg_types = {
                "gp": ' type="data_ptr"',
                "fp": ' type="data_ptr"',
                "sp": ' type="data_ptr"',
                "pc": ' type="code_ptr"',
            }
            reg_xml_lines = []
            for reg in ARC_REGISTERS.keys():
                reg_type = reg_types.get(reg, "")
                reg_xml_lines.append(f'    <reg name="{reg}" bitsize="32"{reg_type}/>' )
            reg_xml = "\n".join(reg_xml_lines)
            target_xml = (
                '<?xml version="1.0"?>\n'
                '<!DOCTYPE target SYSTEM "gdb-target.dtd">\n'
                '<target>\n'
                '  <architecture>arc</architecture>\n'
                '  <feature name="org.gnu.gdb.arc.core">\n'
                f'{reg_xml}\n'
                '  </feature>\n'
                '</target>'
            )
            return f"l{target_xml}"
        else:
            return ""  # Unsupported query

    def handle_packet(self, packet: str):
        """Handle a GDB remote protocol packet"""
        if not packet:
            return

        if packet == "interrupt":
            # Handle Ctrl+C interrupt
            self.send_packet(f"S{self.last_signal:02x}")
            return

        cmd = packet[0]
        args = packet[1:]

        if cmd == "q":
            # Query command
            response = self.handle_query(args)
            if response:
                self.send_packet(response)
            else:
                self.send_packet("")

        elif cmd == "?":
            # Halt reason
            self.send_packet(f"S{self.last_signal:02x}")

        elif cmd == "g":
            # Read all registers using ARC_REGISTERS order
            reg_names = list(ARC_REGISTERS.keys())

            print("GDB requesting all registers")
            values = []
            for reg_name in reg_names:
                value = self.read_register(reg_name)
                values.append(value)
                if reg_name in ["pc", "sp", "fp", "blink", "status32"]:
                    print(f"  {reg_name.upper()}=0x{value:08x}")

            # Pack as little-endian 32-bit values
            reg_data = struct.pack(f"<{len(values)}I", *values)
            self.send_packet(reg_data.hex())

        elif cmd == "p":
            # Read single register
            try:
                reg_num = int(args, 16)
                reg_names = list(ARC_REGISTERS.keys())

                print(
                    f"GDB requesting register {reg_num} ({reg_names[reg_num] if 0 <= reg_num < len(reg_names) else 'unknown'})"
                )

                if 0 <= reg_num < len(reg_names):
                    reg_name = reg_names[reg_num]
                    value = self.read_register(reg_name)
                    print(f"Register {reg_name} = 0x{value:08x}")
                    reg_data = struct.pack("<I", value)
                    self.send_packet(reg_data.hex())
                else:
                    print(f"Unknown register number: {reg_num}, returning 0")
                    # Return 0 for unknown registers instead of error
                    reg_data = struct.pack("<I", 0)
                    self.send_packet(reg_data.hex())
            except ValueError as e:
                print(f"Error parsing register request '{args}': {e}")
                self.send_packet("E01")

        elif cmd == "m":
            # Read memory: m<addr>,<length>
            try:
                addr_len = args.split(",")
                addr = int(addr_len[0], 16)
                length = int(addr_len[1], 16)

                data = self.read_memory(addr, length)
                self.send_packet(data.hex())

            except (ValueError, IndexError):
                self.send_packet("E01")

        elif cmd == "M":
            # Write memory: M<addr>,<length>:<data>
            try:
                addr_rest = args.split(",", 1)
                addr = int(addr_rest[0], 16)
                len_data = addr_rest[1].split(":", 1)
                length = int(len_data[0], 16)
                hex_data = len_data[1]

                data = bytes.fromhex(hex_data)
                if len(data) == length:
                    if self.write_memory(addr, data):
                        self.send_packet("OK")
                    else:
                        self.send_packet("E01")
                else:
                    self.send_packet("E01")

            except (ValueError, IndexError):
                self.send_packet("E01")

        elif cmd == "c":
            # Continue execution
            # For SMC, we can't really control execution, so just return stopped
            self.send_packet(f"S{self.last_signal:02x}")

        elif cmd == "s":
            # Single step
            # Also not supported for SMC, return stopped
            self.send_packet(f"S{self.last_signal:02x}")

        elif cmd == "k":
            # Kill/detach
            self.connected = False

        elif cmd == "D":
            # Detach
            self.send_packet("OK")
            self.connected = False

        else:
            # Unsupported command
            self.send_packet("")

    def run(self):
        """Main server loop"""
        if not self.start_server():
            return 1

        try:
            while self.running:
                if not self.connected:
                    if not self.wait_for_connection():
                        break

                # Handle packets
                while self.connected and self.running:
                    packet = self.receive_packet()
                    if packet is None:
                        print("Client disconnected")
                        self.connected = False
                        break

                    self.handle_packet(packet)

        except KeyboardInterrupt:
            print("\nShutting down server...")
        finally:
            self.stop_server()

        return 0


def parse_args():
    parser = argparse.ArgumentParser(
        description="GDB Remote Server for SMC debugging using pyluwen",
        allow_abbrev=False,
    )
    parser.add_argument(
        "--port", type=int, default=2159, help="TCP port to listen on (default: 2159)"
    )
    parser.add_argument(
        "--asic-id", type=int, default=0, help="ASIC ID to connect to (default: 0)"
    )
    return parser.parse_args()


def main():
    args = parse_args()

    print("GDB Remote Server for SMC (pyluwen)")
    print("===================================")

    server = GDBRemoteServer(asic_id=args.asic_id, port=args.port)
    return server.run()


if __name__ == "__main__":
    sys.exit(main())
