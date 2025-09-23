/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TENSTORRENT_MSGQUEUE_H_
#define TENSTORRENT_MSGQUEUE_H_

#include <stdint.h>

#include <zephyr/sys/iterable_sections.h>

#define NUM_MSG_QUEUES         4
#define MSG_QUEUE_SIZE         4
#define MSG_QUEUE_POINTER_WRAP (2 * MSG_QUEUE_SIZE)
#define REQUEST_MSG_LEN        8
#define RESPONSE_MSG_LEN       8

#define MSG_TYPE_INDEX 0
#define MSG_TYPE_MASK  0xFF
#define MSG_TYPE_SHIFT 0

#define MESSAGE_QUEUE_STATUS_MESSAGE_RECOGNIZED 0xff
#define MESSAGE_QUEUE_STATUS_SCRATCH_ONLY       0xfe

#ifdef __cplusplus
extern "C" {
#endif

struct message_queue_header {
	/* 16B for CPU writes, ARC reads */
	uint32_t request_queue_wptr;
	uint32_t response_queue_rptr;
	uint32_t unused_1;
	uint32_t unused_2;

	/* 16B for ARC writes, CPU reads */
	uint32_t request_queue_rptr;
	uint32_t response_queue_wptr;
	uint32_t last_serial;
	uint32_t unused_3;
};

/**
 * @defgroup tt_msg_apis Host Message Interface
 * @brief Interface for handling host request and response messages between the Tenstorrent host and
 * ARC processor.
 *
 * The host will send a @ref request, specifying the @ref request::command_code (of
 * type @ref msg_type) SMC firmware will parse this message and send back a @ref response.
 *
 * Specific types of requests are parsed via the union members of @ref request and documented
 * therein.
 * @{
 */

/** @brief Host request to force the fan speed */
typedef struct {
	/** @brief The command code corresponding to @ref MSG_TYPE_FORCE_FAN_SPEED*/
	uint8_t command_code;

	/** @brief Three bytes of padding */
	uint8_t pad[3];

	/** @brief The raw speed of the fan to set, as a percentage from 0 to 100 */
	uint32_t raw_speed;
} force_fan_speed_rqst_t;

/** @brief Host request to adjust the AICLK speed
 * @details Requests of this type are processed by @ref aiclk_busy_handler
 */
typedef struct {
	/** @brief The command code corresponding to @ref MSG_TYPE_AICLK_GO_BUSY or @ref
	 * MSG_TYPE_AICLK_GO_LONG_IDLE
	 */
	uint8_t command_code;

	/** @brief Three bytes of padding */
	uint8_t pad[3];
} aiclk_set_speed_rqst_t;

/** @brief Host request to adjust the power settings
 * @details Requests of this type are processed by @ref power_setting_msg_handler
 */
typedef struct {
	/** @brief The command code corresponding to @ref MSG_TYPE_POWER_SETTING*/
	uint8_t command_code;

	/** @brief The number of bits in the @ref power_flags_bitfield that are valid */
	uint8_t power_flags_valid: 4;

	/** @brief The number of fields that are valid in the @ref power_settings_array */
	uint8_t power_settings_valid: 4;

	/** @brief The list of On/Off style power flags SMC supports toggling */
	struct {
		/** @brief 1 - @ref aiclk_set_busy "Set AICLK to Busy" <br>
		 *  0 - @ref aiclk_set_busy "Set AICLK to Idle"
		 */
		uint16_t max_ai_clk: 1;

		/** @brief 1 - @ref set_mrisc_power_setting "Set MRISC power setting to Phy wakeup"
		 * <br> 0 - @ref set_mrisc_power_setting "Set MRISC power setting to Phy Powerdown"
		 */
		uint16_t mrisc_phy_power: 1;

		/** @brief Future use flags currently not supported*/
		uint16_t future_use: 13;

		/** @brief Reserved*/
		uint16_t reserved: 1;
	} power_flags_bitfield;

	struct {
		/** @brief Future use settings currently not supported by SMC*/
		uint16_t future_use[14];
	} power_settings_array;
} power_setting_rqst_t;

/** @brief A tenstorrent host request*/
union request {
	/** @brief The interpretation of the request as an array of uint32_t entries*/
	uint32_t data[REQUEST_MSG_LEN];

	/** @brief The interpretation of the request as just the first byte representing command
	 * code
	 */
	uint8_t command_code;

	/** @brief A force fan speed request*/
	force_fan_speed_rqst_t force_fan_speed;

	/** @brief An AICLK set speed request*/
	aiclk_set_speed_rqst_t aiclk_set_speed;

	/** @brief A power setting request*/
	power_setting_rqst_t power_setting;
};

/** @} */

struct response {
	uint32_t data[RESPONSE_MSG_LEN];
};

typedef uint8_t (*msgqueue_request_handler_t)(const union request *req, struct response *rsp);

struct msgqueue_handler {
	uint32_t msg_type;
	msgqueue_request_handler_t handler;
};

#define REGISTER_MESSAGE(msg, func)                                                                \
	const STRUCT_SECTION_ITERABLE(msgqueue_handler, registration_for_##msg) = {                \
		.msg_type = msg,                                                                   \
		.handler = func,                                                                   \
	}

void process_message_queues(void);
void msgqueue_register_handler(uint32_t msg_code, msgqueue_request_handler_t handler);

int msgqueue_request_push(uint32_t msgqueue_id, const union request *request);
int msgqueue_request_pop(uint32_t msgqueue_id, union request *request);
int msgqueue_response_push(uint32_t msgqueue_id, const struct response *response);
int msgqueue_response_pop(uint32_t msgqueue_id, struct response *response);
void init_msgqueue(void);

#ifdef __cplusplus
}
#endif

#endif
