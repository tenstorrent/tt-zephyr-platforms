/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_avs

#include <stdint.h>

#include <pll.h>

#include <tenstorrent/tt_avs.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

// TODO: log level
LOG_MODULE_REGISTER(tenstorrent_avs, LOG_LEVEL_DBG);

/* Control Registers */
#define APB2AVSBUS_AVS_CMD_REG_OFFSET            0x00
#define APB2AVSBUS_AVS_READBACK_REG_OFFSET       0x04
#define APB2AVSBUS_AVS_FIFOS_STATUS_REG_OFFSET   0x28
#define APB2AVSBUS_AVS_INTERRUPT_MASK_REG_OFFSET 0x34
#define APB2AVSBUS_AVS_CFG_0_REG_OFFSET          0x50
#define APB2AVSBUS_AVS_CFG_1_REG_OFFSET          0x54

/* Field Masks */
#define APB2AVSBUS_AVS_CMD_CMD_GRP_MASK                               0x8000000
#define APB2AVSBUS_AVS_CMD_CMD_CODE_MASK                              0x7800000
#define APB2AVSBUS_AVS_CMD_RAIL_SEL_MASK                              0x780000
#define APB2AVSBUS_AVS_READBACK_CMD_DATA_MASK                         0xFFFF00
#define APB2AVSBUS_AVS_FIFOS_STATUS_CMD_FIFO_VACANT_SLOTS_MASK        0xF00
#define APB2AVSBUS_AVS_FIFOS_STATUS_READBACK_FIFO_OCCUPIED_SLOTS_MASK 0xF0000

/* Field Shifts */
#define APB2AVSBUS_AVS_CMD_CMD_DATA_SHIFT       3
#define APB2AVSBUS_AVS_READBACK_CMD_DATA_SHIFT  8
#define APB2AVSBUS_AVS_CMD_RAIL_SEL_SHIFT       19
#define APB2AVSBUS_AVS_CMD_CMD_CODE_SHIFT       23
#define APB2AVSBUS_AVS_CMD_CMD_GRP_SHIFT        27
#define APB2AVSBUS_AVS_CMD_R_OR_W_SHIFT         28
#define APB2AVSBUS_AVS_READBACK_SLAVE_ACK_SHIFT 30

#define NULL                                 ((void *)0)
#define GET_AVS_FIELD_SHIFT(REG_NAME, FIELD) APB2AVSBUS_AVS_##REG_NAME##_##FIELD##_SHIFT
#define GET_AVS_FIELD_MASK(REG_NAME, FIELD)  APB2AVSBUS_AVS_##REG_NAME##_##FIELD##_MASK
#define AVS_RD_CMD_DATA                      0xffff
#define AVS_FORCE_RESET_DATA                 0x0
#define AVS_RAIL_SEL_BROADCAST               0xf
#define AVS_ERR_RB_DATA                      0xffff
#define AVSCLK_FREQ_MHZ                      20

/* command code, command group macros */
/* 0: defined by AVS spec, 1: vendor specific  */
#define AVS_CMD_VOLTAGE                0x0, 0
#define AVS_CMD_VOUT_TRANS_RATE        0x1, 0
#define AVS_CMD_CURRENT_READ           0x2, 0
#define AVS_CMD_TEMP_READ              0x3, 0
#define AVS_CMD_FORCE_RESET            0x4, 0
#define AVS_CMD_POWER_MODE             0x5, 0
#define AVS_CMD_STATUS                 0xe, 0
#define AVS_CMD_VERSION_READ           0xf, 0
#define AVS_CMD_SYS_INPUT_CURRENT_READ 0x0, 1

#define APB2AVSBUS_AVS_CFG_1_REG_DEFAULT (0x800A0000)

typedef struct {
	uint32_t avs_clock_select: 2;
	uint32_t rsvd_0: 6;
	uint32_t stop_avs_clock_on_idle: 1;
	uint32_t force_slave_resync_operation: 1;
	uint32_t turn_off_all_premux_clocks: 1;
	uint32_t rsvd_1: 5;
	uint32_t clk_divider_value: 8;
	uint32_t clk_divider_duty_cycle_numerator: 8;
} APB2AVSBUS_AVS_CFG_1_reg_t;

typedef union {
	uint32_t val;
	APB2AVSBUS_AVS_CFG_1_reg_t f;
} APB2AVSBUS_AVS_CFG_1_reg_u;

typedef enum {
	AVSOk = 0,
	AVSResourceUnavailable = 1, /* retry */
	AVSBadCrc = 2,              /* retry */
	AVSGoodCrcBadData = 3,      /* no retry */
} AVSStatus;

typedef enum {
	AVSCommitWrite = 0,
	AVSHoldWrite = 1,
	AVSRead = 3,
} AVSReadWriteType;

struct tt_avs_config {
	mm_reg_t base;
};

struct tt_avs_data {
	const struct device *dev;
};

/* Internal Functions */

static inline void tt_avs_wait_cmd_fifo_not_full(const struct device *dev)
{
	const struct tt_avs_config *config = dev->config;
	mm_reg_t reg_base = config->base;

	uint32_t cmd_fifo_vacant_slots = 0;

	do {
		cmd_fifo_vacant_slots =
			sys_read32(reg_base + APB2AVSBUS_AVS_FIFOS_STATUS_REG_OFFSET) &
			GET_AVS_FIELD_MASK(FIFOS_STATUS, CMD_FIFO_VACANT_SLOTS);
	} while (cmd_fifo_vacant_slots == 0);
}

static inline void tt_avs_wait_rx_fifo_not_empty(const struct device *dev)
{
	const struct tt_avs_config *config = dev->config;
	mm_reg_t reg_base = config->base;

	uint32_t readback_fifo_occupied_slots = 0;

	do {
		readback_fifo_occupied_slots =
			sys_read32(reg_base + APB2AVSBUS_AVS_FIFOS_STATUS_REG_OFFSET) &
			GET_AVS_FIELD_MASK(FIFOS_STATUS, READBACK_FIFO_OCCUPIED_SLOTS);
	} while (readback_fifo_occupied_slots == 0);
}

/* Assume users do not program max_retries while reading from the RX FIFO. */
/* TODO: log the debug status in CSM. */
static int tt_avs_read_rx_fifo(const struct device *dev, uint16_t *response)
{
	const struct tt_avs_config *config = dev->config;
	mm_reg_t reg_base = config->base;

	uint8_t max_retries = sys_read32(reg_base + APB2AVSBUS_AVS_CFG_0_REG_OFFSET);
	uint32_t readback_data = 0;
	uint8_t num_tries = 0;
	AVSStatus slave_ack = AVSOk;

	do {
		tt_avs_wait_rx_fifo_not_empty(dev);
		readback_data = sys_read32(reg_base + APB2AVSBUS_AVS_READBACK_REG_OFFSET);
		slave_ack = readback_data >> GET_AVS_FIELD_SHIFT(READBACK, SLAVE_ACK);
		num_tries++;
	} while (slave_ack != AVSOk && num_tries <= max_retries);

	if (response != NULL) {
		if (slave_ack != AVSOk) {
			*response = AVS_ERR_RB_DATA;
		} else {
			*response = (readback_data & GET_AVS_FIELD_MASK(READBACK, CMD_DATA)) >>
				    GET_AVS_FIELD_SHIFT(READBACK, CMD_DATA);
		}
	}

	return (int)slave_ack;
}

static inline void tt_avs_send_cmd(const struct device *dev, uint16_t cmd_data, uint8_t rail_sel,
				   uint8_t cmd_code, uint8_t cmd_grp, AVSReadWriteType r_or_w)
{

	const struct tt_avs_config *config = dev->config;
	mm_reg_t reg_base = config->base;

	tt_avs_wait_cmd_fifo_not_full(dev);
	uint32_t cmd_data_pos = cmd_data << GET_AVS_FIELD_SHIFT(CMD, CMD_DATA);
	uint32_t rail_sel_pos = (rail_sel << GET_AVS_FIELD_SHIFT(CMD, RAIL_SEL)) &
				GET_AVS_FIELD_MASK(CMD, RAIL_SEL);
	uint32_t cmd_code_pos = (cmd_code << GET_AVS_FIELD_SHIFT(CMD, CMD_CODE)) &
				GET_AVS_FIELD_MASK(CMD, CMD_CODE);
	uint32_t cmd_grp_pos =
		(cmd_grp << GET_AVS_FIELD_SHIFT(CMD, CMD_GRP)) & GET_AVS_FIELD_MASK(CMD, CMD_GRP);
	uint32_t r_or_w_pos = r_or_w << GET_AVS_FIELD_SHIFT(CMD, R_OR_W);

	sys_write32(cmd_data_pos | rail_sel_pos | cmd_code_pos | cmd_grp_pos | r_or_w_pos,
		    reg_base + APB2AVSBUS_AVS_CMD_REG_OFFSET);
}

/* Driver API */

static int tt_avs_read_voltage(const struct device *dev, uint8_t rail_sel, uint16_t *voltage_in_mV)
{
	tt_avs_send_cmd(dev, AVS_RD_CMD_DATA, rail_sel, AVS_CMD_VOLTAGE, AVSRead);
	return tt_avs_read_rx_fifo(dev, voltage_in_mV);
}

static int tt_avs_write_voltage(const struct device *dev, uint16_t voltage_in_mV, uint8_t rail_sel)
{
	tt_avs_send_cmd(dev, voltage_in_mV, rail_sel, AVS_CMD_VOLTAGE, AVSCommitWrite);
	int status = tt_avs_read_rx_fifo(dev, NULL);

	/* 150us to cover voltage switch from 0.65V to 0.95V with 50us of margin */
	k_busy_wait(150);
	return status;
}

static int tt_avs_read_vout_trans_rate(const struct device *dev, uint8_t rail_sel,
				       uint8_t *rise_rate, uint8_t *fall_rate)
{
	tt_avs_send_cmd(dev, AVS_RD_CMD_DATA, rail_sel, AVS_CMD_VOUT_TRANS_RATE, AVSRead);

	uint16_t trans_rate;
	int status = tt_avs_read_rx_fifo(dev, &trans_rate);
	*rise_rate = trans_rate >> 8;
	*fall_rate = trans_rate & 0xff;

	return status;
}

static int tt_avs_write_vout_trans_rate(const struct device *dev, uint8_t rise_rate,
					uint8_t fall_rate, uint8_t rail_sel)
{
	uint16_t trans_rate = (rise_rate << 8) | fall_rate;

	tt_avs_send_cmd(dev, trans_rate, rail_sel, AVS_CMD_VOUT_TRANS_RATE, AVSCommitWrite);
	return tt_avs_read_rx_fifo(dev, NULL);
}

static int tt_avs_read_current(const struct device *dev, uint8_t rail_sel, float *current_in_A)
{
	tt_avs_send_cmd(dev, AVS_RD_CMD_DATA, rail_sel, AVS_CMD_CURRENT_READ, AVSRead);

	uint16_t current_in_10mA;
	int status = tt_avs_read_rx_fifo(dev, &current_in_10mA);
	*current_in_A = current_in_10mA * 0.01f;
	return status;
}

static int tt_avs_read_temp(const struct device *dev, uint8_t rail_sel, float *temp_in_C)
{
	tt_avs_send_cmd(dev, AVS_RD_CMD_DATA, rail_sel, AVS_CMD_TEMP_READ, AVSRead);

	uint16_t temp; /* 1LSB = 0.1degC  */
	int status = tt_avs_read_rx_fifo(dev, &temp);
	*temp_in_C = temp * 0.1;
	return status;
}

static int tt_avs_force_voltage_reset(const struct device *dev, uint8_t rail_sel)
{
	tt_avs_send_cmd(dev, AVS_FORCE_RESET_DATA, rail_sel, AVS_CMD_FORCE_RESET, AVSCommitWrite);
	return tt_avs_read_rx_fifo(dev, NULL);
}

static int tt_avs_read_power_mode(const struct device *dev, uint8_t rail_sel,
				  AVSPwrMode *power_mode)
{
	tt_avs_send_cmd(dev, AVS_RD_CMD_DATA, rail_sel, AVS_CMD_POWER_MODE, AVSRead);

	uint16_t power_mode_int;
	int status = tt_avs_read_rx_fifo(dev, &power_mode_int);

	memcpy(&power_mode_int, power_mode, sizeof(power_mode_int));
	return status;
}

static int tt_avs_write_power_mode(const struct device *dev, AVSPwrMode power_mode,
				   uint8_t rail_sel)
{
	tt_avs_send_cmd(dev, power_mode, rail_sel, AVS_CMD_POWER_MODE, AVSCommitWrite);
	return tt_avs_read_rx_fifo(dev, NULL);
}

static int tt_avs_read_status(const struct device *dev, uint8_t rail_sel, uint16_t *status)
{
	tt_avs_send_cmd(dev, AVS_RD_CMD_DATA, rail_sel, AVS_CMD_STATUS, AVSRead);
	return tt_avs_read_rx_fifo(dev, status);
}

static int tt_avs_write_status(const struct device *dev, uint16_t status, uint8_t rail_sel)
{
	tt_avs_send_cmd(dev, status, rail_sel, AVS_CMD_STATUS, AVSCommitWrite);
	return tt_avs_read_rx_fifo(dev, NULL);
}

/* For AVSBus version read, the rail_sel is broadcast. */
/* only the lower 4bits are valid and should be zero for PMBus 1.3. */
/* Any other PMBus versions are not supported by the AVS controller. */
static int tt_avs_read_version(const struct device *dev, uint16_t *version)
{
	tt_avs_send_cmd(dev, AVS_RD_CMD_DATA, AVS_RAIL_SEL_BROADCAST, AVS_CMD_VERSION_READ,
			AVSRead);
	return tt_avs_read_rx_fifo(dev, version);
}

static int tt_avs_read_system_input_current(const struct device *dev, uint16_t *response)
{
	uint8_t rail_sel = 0x0; /* Rail A and Rail B return the same data. */

	tt_avs_send_cmd(dev, AVS_RD_CMD_DATA, rail_sel, AVS_CMD_SYS_INPUT_CURRENT_READ, AVSRead);
	return tt_avs_read_rx_fifo(dev, response);
	/* TODO: need to figure the formula to calculate the system input current */
	/* System Input Current (read only) returns the ADC output of voltage at IINSEN pin. */
	/* The raw ADC data is decoded to determine the VIINSEN voltage: */
	/* VIINSEN (V) = [(ADC in decimal) x 1.1064+43] x 0.001173 – 0.05 */
	/* The actual input current depends on how the current signal is converted to a voltage at
	 * the IINSEN pin. In the case of the MAX20816 EV Kit,
	 */
	/* Input Current (A) = VIINSEN / (RSHUNT x CSA_gain) */
	/* where RSHUNT is the input current sense resistor, and CSA_gain is the gain of the current
	 * sense amplifier.
	 */
}

static int tt_avs_init(const struct device *dev)
{
	const struct tt_avs_config *config = dev->config;
	mm_reg_t reg_base = config->base;

	APB2AVSBUS_AVS_CFG_1_reg_u avs_cfg_1;

	avs_cfg_1.val = APB2AVSBUS_AVS_CFG_1_REG_DEFAULT;
	/* gate all clocks entering AVS clock mux - do this before changeing the clock divider
	 * settings.
	 */
	avs_cfg_1.f.turn_off_all_premux_clocks = 1;
	sys_write32(avs_cfg_1.val, reg_base + APB2AVSBUS_AVS_CFG_1_REG_OFFSET);
	/* use divided version of APB clock as AVS clock, and set the divider value to get a clock
	 * of 20MHz.
	 */
	avs_cfg_1.f.clk_divider_value = DIV_ROUND_UP(GetAPBCLK(), AVSCLK_FREQ_MHZ);
	avs_cfg_1.f.avs_clock_select = 1;
	sys_write32(avs_cfg_1.val, reg_base + APB2AVSBUS_AVS_CFG_1_REG_OFFSET);
	/* enable all clocks entering AVS clock mux. */
	avs_cfg_1.f.turn_off_all_premux_clocks = 0;
	sys_write32(avs_cfg_1.val, reg_base + APB2AVSBUS_AVS_CFG_1_REG_OFFSET);
	/* when AVS bus is idle, gate avs_clock from running. */
	avs_cfg_1.f.stop_avs_clock_on_idle = 1;
	sys_write32(avs_cfg_1.val, reg_base + APB2AVSBUS_AVS_CFG_1_REG_OFFSET);
	k_busy_wait(1);

	/* Enable all interrupts. */
	sys_write32(0, reg_base + APB2AVSBUS_AVS_INTERRUPT_MASK_REG_OFFSET);

	return 0;
}

static const struct avs_driver_api tt_avs_api = {
	.read_voltage = tt_avs_read_voltage,
	.write_voltage = tt_avs_write_voltage,
	.read_vout_trans_rate = tt_avs_read_vout_trans_rate,
	.write_vout_trans_rate = tt_avs_write_vout_trans_rate,
	.read_current = tt_avs_read_current,
	.read_temp = tt_avs_read_temp,
	.force_voltage_reset = tt_avs_force_voltage_reset,
	.read_power_mode = tt_avs_read_power_mode,
	.write_power_mode = tt_avs_write_power_mode,
	.read_status = tt_avs_read_status,
	.write_status = tt_avs_write_status,
	.read_version = tt_avs_read_version,
	.read_system_input_current = tt_avs_read_system_input_current,
};

#define AVS_DEVICE_INIT(n)                                                                         \
	static struct tt_avs_config tt_avs_config_##n = {                                          \
		.base = (mm_reg_t)DT_INST_REG_ADDR(n),                                             \
	};                                                                                         \
                                                                                                   \
	static struct tt_avs_data tt_avs_data_##n;                                                 \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, tt_avs_init, NULL, &tt_avs_data_##n, &tt_avs_config_##n,          \
			      POST_KERNEL, CONFIG_AVS_INIT_PRIORITY, &tt_avs_api);

DT_INST_FOREACH_STATUS_OKAY(AVS_DEVICE_INIT)
