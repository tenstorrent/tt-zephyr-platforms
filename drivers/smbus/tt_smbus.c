/*
 * Copyright (c) 2023 SILA Embedded Solutions GmbH
 * SPDX-License-Identifier: Apache-2.0
 */

#include "smbus_utils.h"

#include <soc.h>
#include <tenstorrent/tt_stm32.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/smbus.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

LOG_MODULE_REGISTER(tt_stm32_smbus, CONFIG_SMBUS_LOG_LEVEL);

struct tt_smbus_stm32_config {
	const struct pinctrl_dev_config *pcfg;
	const struct device *i2c_dev;
};

struct tt_smbus_stm32_data {
	uint32_t config;
	const struct device *dev;
#ifdef CONFIG_SMBUS_STM32_SMBALERT
	sys_slist_t smbalert_callbacks;
	struct k_work smbalert_work;
#endif /* CONFIG_SMBUS_STM32_SMBALERT */
};

#ifdef CONFIG_SMBUS_STM32_SMBALERT
static void tt_smbus_stm32_smbalert_isr(const struct device *dev)
{
	struct tt_smbus_stm32_data *data = dev->data;

	k_work_submit(&data->smbalert_work);
}

static void tt_smbus_stm32_smbalert_work(struct k_work *work)
{
	struct tt_smbus_stm32_data *data =
		CONTAINER_OF(work, struct smbus_stm32_data, smbalert_work);
	const struct device *dev = data->dev;

	LOG_DBG("%s: got SMB alert", dev->name);

	smbus_loop_alert_devices(dev, &data->smbalert_callbacks);
}

static int tt_smbus_stm32_smbalert_set_cb(const struct device *dev, struct smbus_callback *cb)
{
	struct tt_smbus_stm32_data *data = dev->data;

	return smbus_callback_set(&data->smbalert_callbacks, cb);
}

static int tt_smbus_stm32_smbalert_remove_cb(const struct device *dev, struct smbus_callback *cb)
{
	struct tt_smbus_stm32_data *data = dev->data;

	return smbus_callback_remove(&data->smbalert_callbacks, cb);
}
#endif /* CONFIG_SMBUS_STM32_SMBALERT */

void tt_smbus_stm32_set_abort_ptr(const struct device *dev, unsigned int *abort)
{
	const struct tt_smbus_stm32_config *config = dev->config;

	tt_stm32_i2c_set_abort_ptr(config->i2c_dev, abort);
}

static int tt_smbus_stm32_init(const struct device *dev)
{
	const struct tt_smbus_stm32_config *config = dev->config;
	struct tt_smbus_stm32_data *data = dev->data;
	int result;

	data->dev = dev;

	if (!device_is_ready(config->i2c_dev)) {
		LOG_ERR("%s: I2C device is not ready", dev->name);
		return -ENODEV;
	}

	result = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (result < 0) {
		LOG_ERR("%s: pinctrl setup failed (%d)", dev->name, result);
		return result;
	}

#ifdef CONFIG_SMBUS_STM32_SMBALERT
	k_work_init(&data->smbalert_work, smbus_stm32_smbalert_work);

	tt_stm32_i2c_smbalert_set_callback(config->i2c_dev, smbus_stm32_smbalert_isr, dev);
#endif /* CONFIG_SMBUS_STM32_SMBALERT */

	return 0;
}

static int tt_smbus_stm32_configure(const struct device *dev, uint32_t config_value)
{
	const struct tt_smbus_stm32_config *config = dev->config;
	struct tt_smbus_stm32_data *data = dev->data;

	if (config_value & SMBUS_MODE_PEC) {
		LOG_ERR("%s: not implemented", dev->name);
		return -EINVAL;
	}

	if (config_value & SMBUS_MODE_HOST_NOTIFY) {
		LOG_ERR("%s: not available", dev->name);
		return -EINVAL;
	}

	if (config_value & SMBUS_MODE_CONTROLLER) {
		LOG_DBG("%s: configuring SMB in host mode", dev->name);
		tt_stm32_i2c_set_smbus_mode(config->i2c_dev, I2CSTM32MODE_SMBUSHOST);
	} else {
		LOG_DBG("%s: configuring SMB in device mode", dev->name);
		tt_stm32_i2c_set_smbus_mode(config->i2c_dev, I2CSTM32MODE_SMBUSDEVICE);
	}

#ifdef CONFIG_SMBUS_STM32_SMBALERT
	if (config_value & SMBUS_MODE_SMBALERT) {
		LOG_DBG("%s: activating SMB alert", dev->name);
		tt_stm32_i2c_smbalert_enable(config->i2c_dev);
	} else {
		LOG_DBG("%s: deactivating SMB alert", dev->name);
		tt_stm32_i2c_smbalert_disable(config->i2c_dev);
	}
#endif

	data->config = config_value;
	return 0;
}

static int tt_smbus_stm32_get_config(const struct device *dev, uint32_t *config)
{
	struct tt_smbus_stm32_data *data = dev->data;
	*config = data->config;
	return 0;
}

static int tt_smbus_stm32_quick(const struct device *dev, uint16_t periph_addr,
				enum smbus_direction rw)
{
	const struct tt_smbus_stm32_config *config = dev->config;

	switch (rw) {
	case SMBUS_MSG_WRITE:
		return i2c_write(config->i2c_dev, NULL, 0, periph_addr);
	case SMBUS_MSG_READ:
		return i2c_read(config->i2c_dev, NULL, 0, periph_addr);
	default:
		LOG_ERR("%s: invalid smbus direction %i", dev->name, rw);
		return -EINVAL;
	}
}

static int tt_smbus_stm32_byte_write(const struct device *dev, uint16_t periph_addr,
				     uint8_t command)
{
	const struct tt_smbus_stm32_config *config = dev->config;

	return i2c_write(config->i2c_dev, &command, sizeof(command), periph_addr);
}

static int tt_smbus_stm32_byte_read(const struct device *dev, uint16_t periph_addr, uint8_t *byte)
{
	const struct tt_smbus_stm32_config *config = dev->config;

	return i2c_read(config->i2c_dev, byte, sizeof(*byte), periph_addr);
}

static int tt_smbus_stm32_byte_data_write(const struct device *dev, uint16_t periph_addr,
					  uint8_t command, uint8_t byte)
{
	/* Address byte needs to be included */
	uint8_t pec_src[] = {periph_addr << 1 | 0, /* I2C_WRITE_BIT */
			     command, byte};
	uint8_t pec = crc8(pec_src, sizeof(pec_src), 0x07, 0, false);

	const struct tt_smbus_stm32_config *config = dev->config;

	uint8_t buffer[] = {command, byte, pec};

	return i2c_write(config->i2c_dev, buffer, ARRAY_SIZE(buffer), periph_addr);
}

static int tt_smbus_stm32_byte_data_read(const struct device *dev, uint16_t periph_addr,
					 uint8_t command, uint8_t *byte)
{
	const struct tt_smbus_stm32_config *config = dev->config;

	uint8_t buffer[2] = {0};

	int output =
		i2c_write_read(config->i2c_dev, periph_addr, &command, sizeof(command), buffer, 2);

	*byte = buffer[0];

	return output;
}

static int tt_smbus_stm32_word_data_write(const struct device *dev, uint16_t periph_addr,
					  uint8_t command, uint16_t word)
{
	/* Address byte needs to be included */
	uint8_t pec_src[] = {periph_addr << 1 | 0, /* I2C_WRITE_BIT */
			     command, (uint8_t)word & 0xFF, (uint8_t)(word >> 8) & 0xFF};
	uint8_t pec = crc8(pec_src, sizeof(pec_src), 0x07, 0, false);

	const struct tt_smbus_stm32_config *config = dev->config;
	uint8_t buffer[sizeof(command) + sizeof(word) + sizeof(pec)];

	buffer[0] = command;
	sys_put_le16(word, buffer + 1);
	buffer[3] = pec;

	return i2c_write(config->i2c_dev, buffer, ARRAY_SIZE(buffer), periph_addr);
}

static int tt_smbus_stm32_word_data_read(const struct device *dev, uint16_t periph_addr,
					 uint8_t command, uint16_t *word)
{
	const struct tt_smbus_stm32_config *config = dev->config;
	int result;

	result = i2c_write_read(config->i2c_dev, periph_addr, &command, sizeof(command), word,
				sizeof(*word));
	*word = sys_le16_to_cpu(*word);

	return result;
}

static int tt_smbus_stm32_pcall(const struct device *dev, uint16_t periph_addr, uint8_t command,
				uint16_t send_word, uint16_t *recv_word)
{
	const struct tt_smbus_stm32_config *config = dev->config;
	uint8_t buffer[sizeof(command) + sizeof(send_word)];
	int result;

	buffer[0] = command;
	sys_put_le16(send_word, buffer + 1);

	result = i2c_write_read(config->i2c_dev, periph_addr, buffer, ARRAY_SIZE(buffer), recv_word,
				sizeof(*recv_word));
	*recv_word = sys_le16_to_cpu(*recv_word);

	return result;
}

static int tt_smbus_stm32_block_write(const struct device *dev, uint16_t periph_addr,
				      uint8_t command, uint8_t count, uint8_t *buf)
{
	uint8_t pec_src[] = {                      /* Address byte needs to be included */
			     periph_addr << 1 | 0, /* I2C_WRITE_BIT */
			     command, count};
	uint8_t pec = crc8(pec_src, sizeof(pec_src), 0x07, 0, false);

	pec = crc8(buf, count, 0x07, pec, false);

	const struct tt_smbus_stm32_config *config = dev->config;
	struct i2c_msg messages[] = {
		{
			.buf = &command,
			.len = sizeof(command),
			.flags = I2C_MSG_WRITE | I2C_MSG_RESTART,
		},
		{
			.buf = &count,
			.len = sizeof(count),
			.flags = I2C_MSG_WRITE,
		},
		{
			.buf = buf,
			.len = count,
			.flags = I2C_MSG_WRITE,
		},
		{
			.buf = &pec,
			.len = sizeof(pec),
			.flags = I2C_MSG_WRITE | I2C_MSG_STOP,
		},
	};

	return i2c_transfer(config->i2c_dev, messages, ARRAY_SIZE(messages), periph_addr);
}

static int tt_smbus_stm32_block_read(const struct device *dev, uint16_t periph_addr,
				     uint8_t command, uint8_t *count, uint8_t *buf)
{
	int ret;
	uint8_t pec_src[] = {periph_addr << 1 | 1, /* I2C_READ_BIT */
			     command};
	uint8_t pec = crc8(pec_src, sizeof(pec_src), 0x07, 0, false);

	const struct tt_smbus_stm32_config *config = dev->config;
	uint8_t pec_value = 0;

	tt_stm32_i2c_start_transfer(config->i2c_dev);
	ret = tt_stm32_i2c_send_message(config->i2c_dev, periph_addr,
					(struct i2c_msg){
						.buf = &command,
						.len = sizeof(command),
						.flags = I2C_MSG_WRITE | I2C_MSG_RESTART,
					},
					true, false);
	if (ret) {
		goto end_transfer;
	}
	ret = tt_stm32_i2c_send_message(config->i2c_dev, periph_addr,
					(struct i2c_msg){
						.buf = count,
						.len = sizeof(*count),
						.flags = I2C_MSG_READ | I2C_MSG_RESTART,
					},
					false, true);
	if (ret) {
		goto end_transfer;
	}
	if (*count > 32) {
		ret = -ENOBUFS;
		goto end_transfer;
	}
	ret = tt_stm32_i2c_send_message(config->i2c_dev, periph_addr,
					(struct i2c_msg){
						.buf = buf,
						.len = *count,
						.flags = I2C_MSG_READ,
					},
					false, true);
	if (ret) {
		goto end_transfer;
	}
	ret = tt_stm32_i2c_send_message(config->i2c_dev, periph_addr,
					(struct i2c_msg){
						.buf = &pec_value,
						.len = sizeof(pec_value),
						.flags = I2C_MSG_READ | I2C_MSG_STOP,
					},
					false, false);

end_transfer:
	tt_stm32_i2c_stop_transfer(config->i2c_dev);

	/*
	 * if (ret) {
	 *   i2c_recover_bus(config->i2c_dev);
	 *   return ret;
	 * }
	 */

	if (!ret) {
		pec = crc8(count, sizeof(*count), 0x07, pec, false);
		pec = crc8(buf, *count, 0x07, pec, false);

		if (pec != pec_value) {
			return -EINVAL;
		}

		return 0;
	}

	return ret;
}

static const struct smbus_driver_api smbus_stm32_api = {
	.configure = tt_smbus_stm32_configure,
	.get_config = tt_smbus_stm32_get_config,
	.smbus_quick = tt_smbus_stm32_quick,
	.smbus_byte_write = tt_smbus_stm32_byte_write,
	.smbus_byte_read = tt_smbus_stm32_byte_read,
	.smbus_byte_data_write = tt_smbus_stm32_byte_data_write,
	.smbus_byte_data_read = tt_smbus_stm32_byte_data_read,
	.smbus_word_data_write = tt_smbus_stm32_word_data_write,
	.smbus_word_data_read = tt_smbus_stm32_word_data_read,
	.smbus_pcall = tt_smbus_stm32_pcall,
	.smbus_block_write = tt_smbus_stm32_block_write,
	.smbus_block_read = tt_smbus_stm32_block_read,
#ifdef CONFIG_SMBUS_STM32_SMBALERT
	.smbus_smbalert_set_cb = tt_smbus_stm32_smbalert_set_cb,
	.smbus_smbalert_remove_cb = tt_smbus_stm32_smbalert_remove_cb,
#else
	.smbus_smbalert_set_cb = NULL,
	.smbus_smbalert_remove_cb = NULL,
#endif /* CONFIG_SMBUS_STM32_SMBALERT */
	.smbus_block_pcall = NULL,
	.smbus_host_notify_set_cb = NULL,
	.smbus_host_notify_remove_cb = NULL,
};

#define DT_DRV_COMPAT st_tt_stm32_smbus

#define SMBUS_STM32_DEVICE_INIT(n)                                                                 \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	static struct tt_smbus_stm32_config smbus_stm32_config_##n = {                             \
		.i2c_dev = DEVICE_DT_GET(DT_INST_PROP(n, i2c)),                                    \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
	};                                                                                         \
                                                                                                   \
	static struct tt_smbus_stm32_data smbus_stm32_data_##n;                                    \
                                                                                                   \
	SMBUS_DEVICE_DT_INST_DEFINE(n, tt_smbus_stm32_init, NULL, &smbus_stm32_data_##n,           \
				    &smbus_stm32_config_##n, POST_KERNEL,                          \
				    CONFIG_SMBUS_INIT_PRIORITY, &smbus_stm32_api);

DT_INST_FOREACH_STATUS_OKAY(SMBUS_STM32_DEVICE_INIT)
