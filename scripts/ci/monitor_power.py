#!/usr/bin/env python3
#
# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0
#
# Script to monitor board power during test execution using sensors command from kmd driver
# and save to CSV

import argparse
import csv
import subprocess
import sys
import time
import re
import signal
from datetime import datetime


def parse_value_with_unit(value_str, unit_str):
    """
    Parse a value string and unit string into a float value in base units(W, V, A).
    """
    try:
        value = float(value_str)
        unit = (
            unit_str.strip()
        )  # preserve case for units if needed, but usually we just check suffix

        # Power units -> W
        if unit == "kW":
            return value * 1000.0
        elif unit == "mW":
            return value / 1000.0
        elif unit == "uW":
            return value / 1e6
        elif unit == "MW":
            return value * 1e6

        # Voltage units -> V
        elif unit == "mV":
            return value / 1000.0
        elif unit == "uV":
            return value / 1e6
        elif unit == "kV":
            return value * 1000.0
        elif unit == "MV":
            return value * 1e6

        # Current units -> A
        elif unit == "mA":
            return value / 1000.0
        elif unit == "uA":
            return value / 1e6
        elif unit == "kA":
            return value * 1000.0

        return value
    except ValueError:
        return 0.0


def get_power_from_sensors():
    """
    Query board power using the sensors command from kmd driver. Returns power value in watts, or None if query fails.
    """
    try:
        # Run sensors command to get power readings
        result = subprocess.run(["sensors"], capture_output=True, text=True, timeout=5)

        if result.returncode == 0:
            output = result.stdout

            # Look for power readings in the sensors output
            # Format: "power:        XX.XX W" (with spaces before the number)
            # For p150: one power value, for p300: two power values (sum them)
            # We capture the unit to handle kW, mW etc.
            power_pattern = r"power:\s+([\d.]+)\s*([a-zA-Z]+)"
            power_matches = re.findall(power_pattern, output, re.IGNORECASE)

            if power_matches:
                try:
                    # Convert all matches to float and sum them (for p300 with multiple adapters)
                    power_values = [
                        parse_value_with_unit(p[0], p[1]) for p in power_matches
                    ]
                    total_power = sum(power_values)
                    return total_power
                except ValueError:
                    pass

            # Fallback: try other power patterns
            fallback_patterns = [
                r"(?:input|board|total|system)\s*power[:\s]+([\d.]+)\s*([a-zA-Z]+)",
                r"PWR[:\s]+([\d.]+)\s*([a-zA-Z]+)",
            ]

            for pattern in fallback_patterns:
                matches = re.findall(pattern, output, re.IGNORECASE)
                if matches:
                    try:
                        power_values = [
                            parse_value_with_unit(p[0], p[1]) for p in matches
                        ]
                        return sum(power_values)  # Sum in case of multiple matches
                    except ValueError:
                        continue

            # If no direct power match, try to find voltage and current and calculate
            # Format: "vcore:       XXX.XX mV" and "current:      XX.XX A"
            voltage_pattern = r"vcore:\s+([\d.]+)\s*([a-zA-Z]+)"
            current_pattern = r"current:\s+([\d.]+)\s*([a-zA-Z]+)"

            voltage_matches = re.findall(voltage_pattern, output, re.IGNORECASE)
            current_matches = re.findall(current_pattern, output, re.IGNORECASE)

            if voltage_matches and current_matches:
                try:
                    # Sum all voltage and current values (for p300)
                    total_voltage = sum(
                        parse_value_with_unit(v[0], v[1]) for v in voltage_matches
                    )
                    total_current = sum(
                        parse_value_with_unit(c[0], c[1]) for c in current_matches
                    )
                    return total_voltage * total_current
                except (ValueError, IndexError):
                    pass

            return None
    except (subprocess.TimeoutExpired, FileNotFoundError, Exception) as e:
        print(f"Error querying sensors: {e}", file=sys.stderr)
        return None


def monitor_power(output_file, interval=1.0, duration=None):
    """
    Monitor board power and write to CSV file.

    Args:
        output_file: Path to output CSV file
        interval: Sampling interval in seconds (default: 1.0)
        duration: Total monitoring duration in seconds (None = until interrupted)
    """
    # Set up signal handler for graceful shutdown
    stop_monitoring = False

    def signal_handler(signum, frame):
        nonlocal stop_monitoring
        stop_monitoring = True
        print(
            f"\nReceived signal {signum}, stopping monitoring gracefully...",
            file=sys.stderr,
        )

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    start_time = time.time()
    last_sample_time = 0

    with open(output_file, "w", newline="") as csvfile:
        fieldnames = ["timestamp", "elapsed_seconds", "power_watts"]
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()

        print(
            f"Starting power monitoring using sensors command. Output: {output_file}",
            file=sys.stderr,
        )
        print(f"Sampling interval: {interval}s", file=sys.stderr)

        try:
            while not stop_monitoring:
                current_time = time.time()
                elapsed = current_time - start_time

                # Check if duration limit reached
                if duration is not None and elapsed >= duration:
                    break

                # Sample at specified interval
                if current_time - last_sample_time >= interval:
                    power = get_power_from_sensors()
                    timestamp = datetime.now().isoformat()

                    writer.writerow(
                        {
                            "timestamp": timestamp,
                            "elapsed_seconds": f"{elapsed:.2f}",
                            "power_watts": (
                                f"{power:.2f}" if power is not None else "N/A"
                            ),
                        }
                    )
                    csvfile.flush()

                    if power is not None:
                        print(f"[{elapsed:.1f}s] Power: {power:.2f}W", file=sys.stderr)
                    else:
                        print(f"[{elapsed:.1f}s] Power: N/A", file=sys.stderr)

                    last_sample_time = current_time

                # Sleep a bit to avoid busy waiting
                time.sleep(0.1)

        except KeyboardInterrupt:
            # Handle cases where signal interrupts sleep directly or handler wasn't set in time
            stop_monitoring = True
            print("\nPower monitoring interrupted", file=sys.stderr)
        except Exception as e:
            print(f"Error during monitoring: {e}", file=sys.stderr)
            raise

    print(f"Power monitoring completed. Data saved to: {output_file}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(
        description="Monitor board power during test execution using sensors command",
        allow_abbrev=False,
    )
    parser.add_argument(
        "-o",
        "--output",
        type=str,
        default="power_recording.csv",
        help="Output CSV file path (default: power_recording.csv)",
    )
    parser.add_argument(
        "-i",
        "--interval",
        type=float,
        default=1.0,
        help="Sampling interval in seconds (default: 1.0)",
    )
    parser.add_argument(
        "-d",
        "--duration",
        type=float,
        default=None,
        help="Total monitoring duration in seconds (default: until interrupted)",
    )

    args = parser.parse_args()

    monitor_power(args.output, args.interval, args.duration)


if __name__ == "__main__":
    main()
