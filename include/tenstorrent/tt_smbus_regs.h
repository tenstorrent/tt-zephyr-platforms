/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TT_SMBUS_MSGS_H_
#define TT_SMBUS_MSGS_H_

/*
 * This header file contains the definitions for the SMBus registers used to
 * communicate with the CMFW over the SMBus interface. It is also used by
 * the DMFW, as that FW is the SMBus master on PCIe cards.
 * All SMBus registers used by the CMFW should be defined here.
 */

enum CMFWSMBusReg {
	/* RO, 48 bits. Read cm2dmMessage struct describing request from CMFW */
	CMFW_SMBUS_REQ = 0x10,
	/* WO, 16 bits. Write with sequence number and message ID to ack cm2dmMessage */
	CMFW_SMBUS_ACK = 0x11,
	/* WO, 160 bits. Write with dmStaticInfo struct including DMFW version */
	CMFW_SMBUS_DM_STATIC_INFO = 0x20,
	/* WO, 16 bits. Write with 0xA5A5 to respond to CMFW request `kCm2DmMsgIdPing` */
	CMFW_SMBUS_PING = 0x21,
	/* WO, 16 bits. Write with target fan speed percentage (0-100). Used by DMFW to broadcast
	 * forced fan speed to every CMFW so that each chip's telemetry reflects the board-level
	 * setting.
	 */
	CMFW_SMBUS_FAN_SPEED = 0x22,
	/* WO, 16 bits. Write with fan speed to responsd to CMFW request
	 * `kCm2DmMsgIdFanSpeedUpdate` or `kCm2DmMsgIdForcedFanSpeedUpdate`
	 */
	CMFW_SMBUS_FAN_RPM = 0x23,
	/* WO, 16 bits. Write with input power limit for board */
	CMFW_SMBUS_POWER_LIMIT = 0x24,
	/* WO, 16 bits. Write with current input power for board */
	CMFW_SMBUS_POWER_INSTANT = 0x25,
	/* WO, 16 bits. Write with therm trip count */
	CMFW_SMBUS_THERM_TRIP_COUNT = 0x28,
	/* RO, 8 bits. Issue a test read from CMFW scratch register */
	CMFW_SMBUS_TEST_READ = 0xD8,
	/* WO, 8 bits. Write to CMFW scratch register */
	CMFW_SMBUS_TEST_WRITE = 0xD9,
	/* RO, 16 bits. Issue a test read from CMFW scratch register */
	CMFW_SMBUS_TEST_READ_WORD = 0xDA,
	/* WO, 16 bits. Write to CMFW scratch register */
	CMFW_SMBUS_TEST_WRITE_WORD = 0xDB,
	/* RO, 32 bits. Issue a test read from CMFW scratch register */
	CMFW_SMBUS_TEST_READ_BLOCK = 0xDC,
	/* WO, 32 bits. Write to CMFW scratch register */
	CMFW_SMBUS_TEST_WRITE_BLOCK = 0xDD,
	CMFW_SMBUS_MSG_MAX,
};

/* Request IDs that the CMFW can issue within the */

#endif /* TT_SMBUS_MSGS_H_ */
