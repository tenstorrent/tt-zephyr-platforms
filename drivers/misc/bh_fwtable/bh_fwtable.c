/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_bh_fwtable

#include <stddef.h>
#include <stdint.h>

#include <pb_decode.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/device.h>
#include <zephyr/drivers/misc/bh_fwtable.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>

#define BOARDTYPE_ORION 0x37
#define BOARDTYPE_P100  0x36
#define BOARDTYPE_P100A 0x43
#define BOARDTYPE_P150A 0x40
#define BOARDTYPE_P150  0x41
#define BOARDTYPE_P150C 0x42
#define BOARDTYPE_P300  0x44
#define BOARDTYPE_P300A 0x45
#define BOARDTYPE_P300C 0x46
#define BOARDTYPE_UBB   0x47

#define RESET_UNIT_STRAP_REGISTERS_L_REG_ADDR 0x80030D20

LOG_MODULE_REGISTER(bh_fwtable, CONFIG_BH_FWTABLE_LOG_LEVEL);

enum bh_fwtable_e {
	BH_FWTABLE_FLSHINFO,
	BH_FWTABLE_BOARDCFG,
	BH_FWTABLE_CMFWCFG,
};

struct bh_fwtable_config {
	const struct device *flash;
};

struct bh_fwtable_data {
	FwTable fw_table;
	FlashInfoTable flash_info_table;
	ReadOnly read_only_table;
	tt_boot_fs boot_fs_data;
	uint32_t asic_location;
	PcbType pcb_type;
};

int tt_boot_fs_mount_by_device(const struct device *dev);

/* Getter function that returns a const pointer to the fw table */
const FwTable *tt_bh_fwtable_get_fw_table(const struct device *dev)
{
	struct bh_fwtable_data *data = dev->data;

	return &data->fw_table;
}

const FlashInfoTable *tt_bh_fwtable_get_flash_info_table(const struct device *dev)
{
	struct bh_fwtable_data *data = dev->data;

	return &data->flash_info_table;
}

const ReadOnly *tt_bh_fwtable_get_read_only_table(const struct device *dev)
{
	struct bh_fwtable_data *data = dev->data;

	return &data->read_only_table;
}

/* Converts a board id extracted from board type and converts it to a PCB Type */
PcbType tt_bh_fwtable_get_pcb_type(const struct device *dev)
{
	struct bh_fwtable_data *data = dev->data;

	return data->pcb_type;
}

/* Reads GPIO6 to determine whether it is p300 left chip. GPIO6 is only set on p300 left chip. */
bool tt_bh_fwtable_is_p300_left_chip(void)
{
	/* FIXME: use Zephyr's GPIO API instead of bringup code */
	return FIELD_GET(BIT(6), sys_read32(RESET_UNIT_STRAP_REGISTERS_L_REG_ADDR));
}

uint32_t tt_bh_fwtable_get_asic_location(const struct device *dev)
{
	struct bh_fwtable_data *data = dev->data;

	return data->asic_location;
}

/* Loader function that deserializes the fw table bin from the SPI filesystem */
static int tt_bh_fwtable_load(const struct device *dev, enum bh_fwtable_e table)
{
#define BH_FWTABLE_LOADCFG(_enum, _tag, _field, _msgtype)                                          \
	[BH_FWTABLE_##_enum] = {                                                                   \
		.tag = #_tag,                                                                      \
		.offs = offsetof(struct bh_fwtable_data, _field),                                  \
		.size = sizeof(((struct bh_fwtable_data *)0)->_field),                             \
		.msg = &_msgtype##_msg,                                                            \
	}

	uint8_t buffer[MAX(MAX(sizeof(ReadOnly), sizeof(FlashInfoTable)), sizeof(FwTable))];
	size_t bytes_read = 0;
	struct bh_fwtable_data *data = dev->data;
	static const struct loadcfg {
		const char *tag;
		size_t offs;             /* field offset within the bh_fwtable_data struct */
		size_t size;             /* field size within the bh_fwtable_data struct */
		const pb_msgdesc_t *msg; /* pointer to protobuf message */
	} loadcfg[] = {
		BH_FWTABLE_LOADCFG(FLSHINFO, flshinfo, flash_info_table, FlashInfoTable),
		BH_FWTABLE_LOADCFG(BOARDCFG, boardcfg, read_only_table, ReadOnly),
		BH_FWTABLE_LOADCFG(CMFWCFG, cmfwcfg, fw_table, FwTable),
	};

	if (!IS_ENABLED(CONFIG_TT_BH_FWTABLE_HAS_FWCFG) && (table == BH_FWTABLE_CMFWCFG)) {
		/* silently succeed */
		return 0;
	}

	__ASSERT_NO_MSG(table < ARRAY_SIZE(loadcfg));

	int result = tt_boot_fs_get_file(&data->boot_fs_data, loadcfg[table].tag, buffer,
					 sizeof(buffer), &bytes_read);
	if (result != TT_BOOT_FS_OK) {
		LOG_ERR("%s() failed with error code %d", loadcfg[table].tag, result);
		return -EIO;
	}

	/* Convert the binary data to a pb_istream_t that is expected by decode */
	pb_istream_t stream = pb_istream_from_buffer(buffer, bytes_read);
	/* PB_DECODE_NULLTERMINATED: Expect the message to be terminated with zero tag */
	if (!pb_decode_ex(&stream, loadcfg[table].msg, (uint8_t *)data + loadcfg[table].offs,
			  PB_DECODE_NULLTERMINATED)) {
		LOG_ERR("%s() failed: '%s'", "pb_decode_ex", loadcfg[table].tag);
		return -EINVAL;
	}

	LOG_DBG("Loaded %s", loadcfg[table].tag);
	return 0;
}

static int tt_bh_fwtable_init(const struct device *dev)
{
	int ret;
	uint8_t board_type;
	struct bh_fwtable_data *data = dev->data;
	const struct bh_fwtable_config *config = dev->config;

	if (!device_is_ready(config->flash)) {
		LOG_ERR("Flash device is not ready");
		return -ENODEV;
	}

	ret = tt_boot_fs_mount_by_device(dev);
	if (ret < 0) {
		LOG_ERR("%s() failed: %d", "tt_boot_fs_mount_by_device", ret);
		return ret;
	}

	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		ret = tt_bh_fwtable_load(dev, BH_FWTABLE_BOARDCFG);
	} else {
		ret = tt_bh_fwtable_load(dev, BH_FWTABLE_FLSHINFO) ||
		      tt_bh_fwtable_load(dev, BH_FWTABLE_BOARDCFG) ||
		      tt_bh_fwtable_load(dev, BH_FWTABLE_CMFWCFG);
	}
	if (ret < 0) {
		LOG_ERR("%s() failed: %d", "tt_bh_fwtable_load", ret);
		return ret;
	}

	/* Extract board type from board_id */
	board_type = (uint8_t)((data->read_only_table.board_id >> 36) & 0xFF);

	/* Figure out PCB type from board type */
	switch (board_type) {
	case BOARDTYPE_ORION:
		data->pcb_type = PcbTypeOrion;
		break;
	case BOARDTYPE_P100:
		data->pcb_type = PcbTypeP100;
		break;
	/* Note: the P100A is a depopulated P150, so PcbType is actually P150 */
	/* eth will be all disabled as per P100 specs anyways */
	case BOARDTYPE_P100A:
	case BOARDTYPE_P150:
	case BOARDTYPE_P150A:
	case BOARDTYPE_P150C:
		data->pcb_type = PcbTypeP150;
		break;
	case BOARDTYPE_P300:
	case BOARDTYPE_P300A:
	case BOARDTYPE_P300C:
		data->pcb_type = PcbTypeP300;
		break;
	case BOARDTYPE_UBB:
		data->pcb_type = PcbTypeUBB;
		break;
	default:
		LOG_WRN("Unknown board type: 0x%x", board_type);
		break;
	}

	switch (data->pcb_type) {
	case PcbTypeP100:
	case PcbTypeP150:
		data->asic_location = 0;
	case PcbTypeP300:
		data->asic_location = (uint32_t)tt_bh_fwtable_is_p300_left_chip();
	case PcbTypeUBB: {
		data->asic_location = data->read_only_table.asic_location;
	}
	default:
		LOG_WRN("Unknown pcb type: 0x%x", data->pcb_type);
		break;
	}

	return 0;
}

#define BH_FWTABLE_INIT(_inst)                                                                     \
	static const struct bh_fwtable_config bh_fwtable_config_##_inst = {                        \
		.flash = DEVICE_DT_GET(DT_INST_PHANDLE(_inst, flash_dev)),                         \
	};                                                                                         \
	static struct bh_fwtable_data bh_fwtable_data_##_inst = {                                  \
		.pcb_type = PcbTypeUnknown,                                                        \
		.asic_location = UINT32_MAX,                                                       \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(_inst, tt_bh_fwtable_init, NULL, &bh_fwtable_data_##_inst,           \
			      &bh_fwtable_config_##_inst, POST_KERNEL,                             \
			      CONFIG_BH_FWTABLE_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(BH_FWTABLE_INIT)
