#!/usr/bin/env python3
#
# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0
"""
Script to parse CTF data and convert it to the Chrome tracing format.
format.

Example usage:
    ./scripts/tracing/ctf_to_chrome.py -t ctf_data_directory
"""

import argparse
import json
from pathlib import Path
import sys

try:
    import bt2
except ImportError:
    sys.exit("Missing dependency: You need to install python bindings of babeltrace.")
try:
    from elftools.elf.elffile import ELFFile
except ImportError:
    sys.exit("Missing dependency: You need to install pyelftools")

THREAD_EVENT_NAME = "Thread Active"


def parse_args():
    """
    Parse command line arguments.
    @return: Parsed arguments
    """
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
    )
    parser.add_argument(
        "-t",
        "--trace",
        required=True,
        help="tracing data (directory with metadata and trace file)",
    )
    parser.add_argument(
        "-o", "--output", required=True, help="output file for Chrome tracing format"
    )
    parser.add_argument(
        "-e", "--elf", type=Path, help="ELF file for symbol resolution (optional)"
    )
    args = parser.parse_args()
    return args


def serialize_bt2_value(value):
    """
    Serialize a Babeltrace value to a JSON-compatible format.
    @param value: Babeltrace value to serialize
    @return: JSON-compatible representation of the value
    """
    if isinstance(value, bt2._IntegerFieldConst):
        return int(value)
    if isinstance(value, bt2._UnsignedIntegerFieldConst):
        return int(value)
    if isinstance(value, bt2._StringFieldConst):
        return str(value)
    if isinstance(value, bt2._BoolFieldConst):
        return bool(value)
    if isinstance(value, bt2._StructureFieldConst):
        return {k: serialize_bt2_value(v) for k, v in value.items()}

    raise TypeError(f"Unsupported Babeltrace value type: {type(value)}")


def main():
    """
    Main function to process the CTF trace and convert it to Chrome tracing format.
    """
    args = parse_args()

    ns_from_origin = 0
    last_timestamp = 0
    ns_adjustment = 0

    msg_it = bt2.TraceCollectionMessageIterator(args.trace)

    chrome_events = []
    symbols_table = {}

    # Tracks running thread name
    active_thread = "none"

    if args.elf is not None and args.elf.exists():
        # Load the ELF file for symbol resolution
        with open(args.elf, "rb") as elf_file:
            elf = ELFFile(elf_file)
            if not elf.has_dwarf_info():
                sys.exit("ELF file does not contain DWARF information.")
            for section in elf.iter_sections():
                if section.name == ".symtab":
                    for symbol in section.iter_symbols():
                        if symbol.name:
                            # Map symbol addresses to names
                            symbols_table[symbol.entry.st_value] = symbol.name

    for msg in msg_it:
        if not isinstance(msg, bt2._EventMessageConst):
            continue

        ####### NOTE #######
        # The below logic assumes that there will always be an event
        # at least every 1073741822 nanoseconds (about 1.07 seconds).
        # We could also fix this by using the timer tick value directly as a
        # timestamp, but that would require a change to the CTF trace generation.
        # and a custom trace metadata file.
        ####################
        if msg.default_clock_snapshot.ns_from_origin - last_timestamp > 0x3FFFFFFE:
            # This error is caused when the ARC timer value wraps around. At 800MHz,
            # this occurs when the reported timestamp in nanoseconds is 0x13ffffffe.
            # To correct this, subtract 0xffffffff - 0x3ffffffe from the current timestamp.
            ns_adjustment += 0xFFFFFFFF - 0x3FFFFFFE

        last_timestamp = msg.default_clock_snapshot.ns_from_origin
        ns_from_origin = msg.default_clock_snapshot.ns_from_origin - ns_adjustment
        event = msg.event

        if event.payload_field:
            event_args = serialize_bt2_value(event.payload_field)
        else:
            event_args = {}
        # Useful for correlating events with the original CTF trace
        event_args["raw_timestamp"] = last_timestamp
        chrome_event = {
            "args": event_args,
            "name": event.name,
            "ph": "I",  # Instant event
            "ts": ns_from_origin // 1000,  # Convert to microseconds
            "pid": active_thread,
            "tid": active_thread,
        }

        if event.name in [
            "thread_switched_out",
            "thread_switched_in",
            "thread_pending",
            "thread_ready",
            "thread_resume",
            "thread_suspend",
            "thread_create",
            "thread_abort",
        ]:
            if event.name in ["thread_switched_in", "idle"]:
                # Create an end event for the previous active thread
                end_event = chrome_event.copy()
                end_event["name"] = THREAD_EVENT_NAME
                end_event["ph"] = "E"  # End event
                end_event["cat"] = "thread"
                chrome_events.append(end_event)
                # Create a new start event for the new active thread
                start_event = end_event.copy()
                # Idle thread is a special case, sometimes it actives and immediately
                # interrupts before it can trace a thread switch in.
                if event.name == "idle":
                    active_thread = "idle"
                else:
                    active_thread = serialize_bt2_value(
                        event.payload_field.get("name", 0)
                    )
                start_event["tid"] = active_thread
                start_event["pid"] = active_thread
                start_event["ph"] = "B"  # Begin event
                chrome_event["tid"] = active_thread
                chrome_event["pid"] = active_thread
                chrome_events.append(start_event)

            chrome_event["cat"] = "thread"
        elif event.name in [
            "semaphore_init",
            "semaphore_take_enter",
            "semaphore_take_exit",
            "semaphore_take_blocking",
            "semaphore_reset",
            "semaphore_give_enter",
            "semaphore_give_exit",
        ]:
            chrome_event["cat"] = "semaphore"
            # Try to resolve the semaphore name if available
            if event.payload_field.get("id") in symbols_table:
                chrome_event["args"]["name"] = symbols_table[event.payload_field["id"]]
        elif event.name in [
            "mutex_init",
            "mutex_lock_blocking",
            "mutex_lock_enter",
            "mutex_lock_exit",
            "mutex_unlock_enter",
            "mutex_unlock_exit",
        ]:
            chrome_event["cat"] = "mutex"
            if event.payload_field.get("id") in symbols_table:
                chrome_event["args"]["name"] = symbols_table[event.payload_field["id"]]
        elif event.name in [
            "timer_init",
            "timer_start",
            "timer_stop",
            "timer_status_sync_enter",
            "timer_status_sync_exit",
        ]:
            chrome_event["cat"] = "timer"
            if event.payload_field.get("id") in symbols_table:
                chrome_event["args"]["name"] = symbols_table[event.payload_field["id"]]
        elif event.name in ["named_event"]:
            chrome_event["name"] = serialize_bt2_value(
                event.payload_field.get("name", "unnamed_event")
            )
            chrome_event["cat"] = "named_event"

        chrome_events.append(chrome_event)

    # Output the events in Chrome tracing format
    with open(args.output, "w") as f:
        json.dump(chrome_events, f)
        print(f"Processed {len(chrome_events)} events")


if __name__ == "__main__":
    main()
