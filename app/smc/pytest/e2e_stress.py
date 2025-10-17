#!/bin/env python3

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import time
import subprocess
from pathlib import Path

import pyluwen

from e2e_smoke import (
    dirty_reset_test,
    smi_reset_test,
    arc_watchdog_test,
    pcie_fw_load_time_test,
)

# Needed to keep ruff from complaining about this "unused import"
# ruff: noqa: F811
from e2e_smoke import arc_chip_dut, launched_arc_dut  # noqa: F401

logger = logging.getLogger(__name__)

SCRIPT_DIR = Path(os.path.dirname(os.path.abspath(__file__)))

# Constant memory addresses we can read from SMC
PING_DMFW_DURATION_REG_ADDR = 0x80030448

# ARC messages
ARC_MSG_TYPE_TEST = 0x90
ARC_MSG_TYPE_PING_DM = 0xC0
ARC_MSG_TYPE_SET_WDT = 0xC1
ARC_MSG_TYPE_READ_TS = 0x1B  # TT_SMC_MSG_READ_TS
ARC_MSG_TYPE_READ_PD = 0x1C  # TT_SMC_MSG_READ_PD
ARC_MSG_TYPE_READ_VM = 0x1D  # TT_SMC_MSG_READ_VM

# Lower this number if testing local changes, so that tests run faster.
MAX_TEST_ITERATIONS = 1000

# Number of PVT sensors
NUM_TS = 8   # Temperature sensors
NUM_PD = 16  # Process detectors
NUM_VM = 8   # Voltage monitors


def report_results(test_name, fail_count, total_tries):
    """
    Helper function to log the results of a test. This uses a
    consistent format so that twister can parse the results
    """
    logger.info(f"{test_name} completed. Failed {fail_count}/{total_tries} times.")


def tt_smi_reset():
    """
    Resets the SMC using tt-smi
    """
    smi_reset_cmd = "tt-smi -r"
    smi_reset_result = subprocess.run(
        smi_reset_cmd.split(), capture_output=True, check=False
    ).returncode
    return smi_reset_result


def test_arc_watchdog(arc_chip_dut, asic_id):
    """
    Validates that the DMC firmware watchdog for the ARC will correctly
    reset the chip
    """
    # todo: find better way to get test name
    test_name = "ARC watchdog test"
    total_tries = min(MAX_TEST_ITERATIONS, 100)
    fail_count = 0
    failure_fail_count = 0

    for i in range(total_tries):
        if i % 10 == 0:
            logger.info(f"{test_name} iteration {i}/{total_tries}")

        result = arc_watchdog_test(asic_id)
        if not result:
            logger.warning(f"{test_name} failed on iteration {i}")
            fail_count += 1

    report_results(test_name, fail_count, total_tries)
    assert fail_count <= failure_fail_count, (
        f"{test_name} failed {fail_count}/{total_tries} times."
    )


def test_pcie_fw_load_time(arc_chip_dut, asic_id):
    """
    Checks PCIe firmware load time is within 40ms.
    This test needs to be run after production reset.
    """
    # todo: find better way to get test name
    test_name = "PCIe firmware load time test"
    total_tries = min(MAX_TEST_ITERATIONS, 10)
    fail_count = 0
    failure_fail_count = 0

    for i in range(total_tries):
        logger.info(
            f"Starting PCIe firmware load time test iteration {i}/{total_tries}"
        )
        # Reset the SMC to ensure we have a clean state
        if tt_smi_reset() != 0:
            logger.warning(f"tt-smi reset failed on iteration {i}")
            fail_count += 1
            continue
        result = pcie_fw_load_time_test(asic_id)
        if not result:
            logger.warning(f"PCIe firmware load time test failed on iteration {i}")
            fail_count += 1

    report_results(test_name, fail_count, total_tries)
    assert fail_count <= failure_fail_count, (
        f"{test_name} failed {fail_count}/{total_tries} times."
    )


def test_smi_reset(arc_chip_dut, asic_id):
    """
    Checks that tt-smi resets are working successfully
    """
    # todo: find better way to get test name
    test_name = "tt-smi reset test"
    total_tries = min(MAX_TEST_ITERATIONS, 1000)
    fail_count = 0
    failure_fail_count = total_tries // 100
    dmfw_ping_avg = 0
    dmfw_ping_max = 0
    for i in range(total_tries):
        if i % 10 == 0:
            logger.info(f"{test_name} iteration {i}/{total_tries}")

        result = smi_reset_test(asic_id)

        if not result:
            logger.warning(f"tt-smi reset failed on iteration {i}")
            fail_count += 1
            continue

        arc_chip = pyluwen.detect_chips()[asic_id]
        response = arc_chip.arc_msg(ARC_MSG_TYPE_PING_DM, True, False, 0, 0, 1000)
        if response[0] != 1 or response[1] != 0:
            logger.warning(f"Ping failed on iteration {i}")
            fail_count += 1
        duration = arc_chip.axi_read32(PING_DMFW_DURATION_REG_ADDR)
        dmfw_ping_avg += duration / total_tries
        dmfw_ping_max = max(dmfw_ping_max, duration)

    logger.info(
        f"Average DMFW ping time (after reset): {dmfw_ping_avg:.2f} ms, "
        f"Max DMFW ping time (after reset): {dmfw_ping_max:.2f} ms."
    )

    report_results(test_name, fail_count, total_tries)
    assert fail_count <= failure_fail_count, (
        f"{test_name} failed {fail_count}/{total_tries} times."
    )


def test_dirty_reset():
    """
    Checks that the SMC comes up correctly after a "dirty" reset, where the
    DMC resets without the SMC requesting it. This is similar to the conditions
    that might be encountered after a NOC hang
    """
    test_name = "Dirty reset test"
    total_tries = min(MAX_TEST_ITERATIONS, 1000)
    fail_count = 0
    failure_fail_count = total_tries // 100

    for i in range(total_tries):
        if i % 10 == 0:
            logger.info(f"{test_name} iteration {i}/{total_tries}")

        result = dirty_reset_test()
        if not result:
            logger.warning(f"dirty reset failed on iteration {i}")
            fail_count += 1
        else:
            # Delay a moment before next run. Without this, tests seem to fail
            # TODO- would be best to determine why rapidly resetting like this
            # breaks enumeration.
            time.sleep(0.5)

    report_results(test_name, fail_count, total_tries)
    assert fail_count <= failure_fail_count, (
        f"{test_name} failed {fail_count}/{total_tries} times."
    )


def test_dmc_ping(arc_chip_dut, asic_id):
    """
    Repeatedly pings the DMC from the SMC to see what the average response time
    is. Ping statistics are printed to the log. These statistics are gathered
    without resetting the SMC. The `smi_reset` test will gather statistics
    for the SMC reset case.
    """
    arc_chip = pyluwen.detect_chips()[asic_id]
    total_tries = min(MAX_TEST_ITERATIONS, 1000)
    fail_count = 0
    dmfw_ping_avg = 0
    dmfw_ping_max = 0
    for i in range(total_tries):
        response = arc_chip.arc_msg(ARC_MSG_TYPE_PING_DM, True, False, 0, 0, 1000)
        if response[0] != 1 or response[1] != 0:
            logger.warning(f"Ping failed on iteration {i}")
            fail_count += 1
        duration = arc_chip.axi_read32(PING_DMFW_DURATION_REG_ADDR)
        dmfw_ping_avg += duration / total_tries
        dmfw_ping_max = max(dmfw_ping_max, duration)
    logger.info(
        f"Ping statistics: {total_tries - fail_count} successful pings, "
        f"{fail_count} failed pings."
    )
    # Recalculate the average ping time
    logger.info(
        f"Average DMFW ping time: {dmfw_ping_avg:.2f} ms, "
        f"Max DMFW ping time: {dmfw_ping_max:.2f} ms."
    )
    report_results("DMC ping test", fail_count, total_tries)
    assert fail_count == 0, "DMC ping test failed a non-zero number of times."


def test_temperature_sensors(arc_chip_dut, asic_id):
    test_name = "Temperature sensor (TS) test"
    arc_chip = pyluwen.detect_chips()[asic_id]
    total_tries = min(MAX_TEST_ITERATIONS, 100)
    fail_count = 0
    
    temp_readings = []
    
    for i in range(total_tries):
        if i % 10 == 0:
            logger.info(f"{test_name} iteration {i}/{total_tries}")
        
        iteration_success = True
        iteration_temps = []
        
        # Test all temperature sensors
        for sensor_id in range(NUM_TS):
            try:
                # Send ARC message: READ_TS with sensor_id
                # Message format: arc_msg(msg_type, wait_for_response, arg_32bit, arg1, arg2, timeout_ms)
                response = arc_chip.arc_msg(ARC_MSG_TYPE_READ_TS, True, False, sensor_id, 0, 5000)
                
                # Check if message was successful
                if response[0] != 1:  # response[0] should be 1 for success
                    logger.warning(f"TS sensor {sensor_id} read failed on iteration {i}, response: {response}")
                    iteration_success = False
                    continue
                
                # Extract readings from response
                # Based on pvt.c: response[1] = raw reading, response[2] = temperature in telemetry format
                raw_reading = response[1]
                temp_telemetry = response[2]
                
                # Validate readings are reasonable
                if raw_reading == 0 or temp_telemetry == 0:
                    logger.warning(f"TS sensor {sensor_id} returned zero readings on iteration {i}")
                    iteration_success = False
                    continue
                
                # Convert telemetry format back to temperature (if needed for validation)
                # For basic validation, just check the readings are non-zero and in reasonable range
                if raw_reading > 0xFFFF or temp_telemetry > 0xFFFF:
                    logger.warning(f"TS sensor {sensor_id} readings out of range on iteration {i}")
                    iteration_success = False
                    continue
                
                iteration_temps.append({
                    'sensor_id': sensor_id,
                    'raw': raw_reading,
                    'telemetry': temp_telemetry
                })
                
            except Exception as e:
                logger.warning(f"TS sensor {sensor_id} exception on iteration {i}: {e}")
                iteration_success = False
        
        if not iteration_success:
            fail_count += 1
        else:
            temp_readings.append(iteration_temps)
    
    # Log some statistics
    if temp_readings:
        logger.info(f"Successfully read {len(temp_readings)} temperature sensor iterations")
        # Log first successful reading as example
        first_reading = temp_readings[0]
        for sensor_data in first_reading:
            logger.info(f"TS{sensor_data['sensor_id']}: raw=0x{sensor_data['raw']:04X}, "
                       f"telemetry=0x{sensor_data['telemetry']:04X}")
    
    report_results(test_name, fail_count, total_tries)
    assert fail_count, (
        f"{test_name} failed {fail_count}/{total_tries} times"
    )


def test_process_detectors(arc_chip_dut, asic_id):
    test_name = "Process detector (PD) test"
    arc_chip = pyluwen.detect_chips()[asic_id]
    total_tries = min(MAX_TEST_ITERATIONS, 50)
    fail_count = 0
    
    # Test different delay chains (based on aging measurement)
    delay_chains = [19, 20, 21]  # From pvt_tt_bh.c: ALL_AGING_OSC 0x7
    pd_readings = []
    
    for i in range(total_tries):
        if i % 10 == 0:
            logger.info(f"{test_name} iteration {i}/{total_tries}")
        
        iteration_success = True
        iteration_data = []
        
        for delay_chain in delay_chains:
            # Test a subset of PD sensors (testing all 16 would be too slow)
            test_sensors = [0, 4, 8, 12]  # Test every 4th sensor
            
            for sensor_id in test_sensors:
                try:
                    # Send ARC message: READ_PD with delay_chain and sensor_id
                    # Based on pvt.c: request->data[1] = delay_chain, request->data[2] = sensor_id
                    response = arc_chip.arc_msg(ARC_MSG_TYPE_READ_PD, True, False, delay_chain, sensor_id, 5000)
                    
                    if response[0] != 1:
                        logger.warning(f"PD sensor {sensor_id} delay_chain {delay_chain} read failed on iteration {i}")
                        iteration_success = False
                        continue
                    
                    # Extract readings: response[1] = raw, response[2] = frequency in telemetry format
                    raw_reading = response[1]
                    freq_telemetry = response[2]
                    
                    if raw_reading == 0 or freq_telemetry == 0:
                        logger.warning(f"PD sensor {sensor_id} returned zero readings on iteration {i}")
                        iteration_success = False
                        continue
                    
                    iteration_data.append({
                        'sensor_id': sensor_id,
                        'delay_chain': delay_chain,
                        'raw': raw_reading,
                        'freq_telemetry': freq_telemetry
                    })
                    
                except Exception as e:
                    logger.warning(f"PD sensor {sensor_id} exception on iteration {i}: {e}")
                    iteration_success = False
        
        if not iteration_success:
            fail_count += 1
        else:
            pd_readings.append(iteration_data)
    
    # Log statistics
    if pd_readings:
        logger.info(f"Successfully read {len(pd_readings)} process detector iterations")
        first_reading = pd_readings[0]
        for pd_data in first_reading[:3]:  # Log first few as examples
            logger.info(f"PD{pd_data['sensor_id']} chain{pd_data['delay_chain']}: "
                       f"raw=0x{pd_data['raw']:04X}, freq=0x{pd_data['freq_telemetry']:04X}")
    
    report_results(test_name, fail_count, total_tries)
    assert fail_count, (
        f"{test_name} failed {fail_count}/{total_tries} times"
    )


def test_voltage_monitors(arc_chip_dut, asic_id):
    test_name = "Voltage monitor (VM) test"
    arc_chip = pyluwen.detect_chips()[asic_id]
    total_tries = min(MAX_TEST_ITERATIONS, 100)
    fail_count = 0
    
    vm_readings = []
    
    for i in range(total_tries):
        if i % 10 == 0:
            logger.info(f"{test_name} iteration {i}/{total_tries}")
        
        iteration_success = True
        iteration_voltages = []
        
        # Test all voltage monitors
        for sensor_id in range(NUM_VM):
            try:
                # Send ARC message: READ_VM with sensor_id
                response = arc_chip.arc_msg(ARC_MSG_TYPE_READ_VM, True, False, sensor_id, 0, 5000)
                
                if response[0] != 1:
                    logger.warning(f"VM sensor {sensor_id} read failed on iteration {i}")
                    iteration_success = False
                    continue
                
                # Extract readings: response[1] = raw, response[2] = voltage in mV
                raw_reading = response[1]
                voltage_mv = response[2]
                
                if raw_reading == 0:
                    logger.warning(f"VM sensor {sensor_id} returned zero raw reading on iteration {i}")
                    iteration_success = False
                    continue
                
                # Validate voltage is in reasonable range (e.g., 0.5V to 2.0V = 500mV to 2000mV)
                if voltage_mv < 300 or voltage_mv > 2500:
                    logger.warning(f"VM sensor {sensor_id} voltage {voltage_mv}mV out of range on iteration {i}")
                    iteration_success = False
                    continue
                
                iteration_voltages.append({
                    'sensor_id': sensor_id,
                    'raw': raw_reading,
                    'voltage_mv': voltage_mv
                })
                
            except Exception as e:
                logger.warning(f"VM sensor {sensor_id} exception on iteration {i}: {e}")
                iteration_success = False
        
        if not iteration_success:
            fail_count += 1
        else:
            vm_readings.append(iteration_voltages)
    
    # Log statistics
    if vm_readings:
        logger.info(f"Successfully read {len(vm_readings)} voltage monitor iterations")
        first_reading = vm_readings[0]
        for vm_data in first_reading:
            logger.info(f"VM{vm_data['sensor_id']}: raw=0x{vm_data['raw']:04X}, "
                       f"voltage={vm_data['voltage_mv']}mV")
    
    report_results(test_name, fail_count, total_tries)
    assert fail_count, (
        f"{test_name} failed {fail_count}/{total_tries} times"
    )


def test_tmon_comprehensive(arc_chip_dut, asic_id):
    test_name = "Comprehensive PVT test"
    arc_chip = pyluwen.detect_chips()[asic_id]
    total_tries = min(MAX_TEST_ITERATIONS, 20)
    fail_count = 0
    
    for i in range(total_tries):
        logger.info(f"{test_name} iteration {i}/{total_tries}")
        
        iteration_success = True
        
        try:
            # Test one sensor from each type
            test_sensors = [
                (ARC_MSG_TYPE_READ_TS, 0, "Temperature"),  # TS sensor 0
                (ARC_MSG_TYPE_READ_PD, 19, "Process"),     # PD with delay chain 19, sensor 0 
                (ARC_MSG_TYPE_READ_VM, 0, "Voltage")       # VM sensor 0
            ]
            
            readings = {}
            
            for msg_type, sensor_param, sensor_name in test_sensors:
                if msg_type == ARC_MSG_TYPE_READ_PD:
                    # PD needs both delay_chain and sensor_id
                    response = arc_chip.arc_msg(msg_type, True, False, sensor_param, 0, 5000)
                else:
                    # TS and VM only need sensor_id
                    response = arc_chip.arc_msg(msg_type, True, False, sensor_param, 0, 5000)
                
                if response[0] != 1:
                    logger.warning(f"{sensor_name} sensor read failed on iteration {i}")
                    iteration_success = False
                    break
                
                readings[sensor_name] = {
                    'raw': response[1],
                    'processed': response[2]
                }
            
            if iteration_success:
                # Log successful comprehensive reading
                if i == 0:  # Log first successful reading as example
                    logger.info("Comprehensive PVT reading example:")
                    for sensor_type, data in readings.items():
                        logger.info(f"  {sensor_type}: raw=0x{data['raw']:04X}, "
                                   f"processed=0x{data['processed']:04X}")
        
        except Exception as e:
            logger.warning(f"Comprehensive PVT test exception on iteration {i}: {e}")
            iteration_success = False
        
        if not iteration_success:
            fail_count += 1
    
    report_results(test_name, fail_count, total_tries)
    assert fail_count, (
        f"{test_name} failed {fail_count}/{total_tries} times"
    )
