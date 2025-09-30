/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_bh_fwtable

#include <stddef.h>

#include <pb_decode.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/misc/bh_fwtable.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

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
};

/* Getter function that returns a const pointer to the fw table */
const FwTable *tt_bh_fwtable_get_fw_table(const struct device *dev)
{
	struct bh_fwtable_data *data = dev->data;

	if (!device_is_ready(dev) && !IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		LOG_DBG("%s table has not been loaded", "Firmware");
	}
	return &data->fw_table;
}

const FlashInfoTable *tt_bh_fwtable_get_flash_info_table(const struct device *dev)
{
	struct bh_fwtable_data *data = dev->data;

	if (!device_is_ready(dev) && !IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		LOG_DBG("%s table has not been loaded", "Flash Info");
	}
	return &data->flash_info_table;
}

const ReadOnly *tt_bh_fwtable_get_read_only_table(const struct device *dev)
{
	struct bh_fwtable_data *data = dev->data;

	if (!device_is_ready(dev) && !IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		LOG_DBG("%s table has not been loaded", "Read Only");
	}
	return &data->read_only_table;
}

/* Converts a board id extracted from board type and converts it to a PCB Type */
PcbType tt_bh_fwtable_get_pcb_type(const struct device *dev)
{
	PcbType pcb_type;
	struct bh_fwtable_data *data = dev->data;

	if (!device_is_ready(dev)) {
		return PcbTypeUnknown;
	}

	/* Extract board type from board_id */
	uint8_t board_type = (uint8_t)((data->read_only_table.board_id >> 36) & 0xFF);

	/* Figure out PCB type from board type */
	switch (board_type) {
	case BOARDTYPE_ORION:
		pcb_type = PcbTypeOrion;
		break;
	case BOARDTYPE_P100:
		pcb_type = PcbTypeP100;
		break;
	/* Note: the P100A is a depopulated P150, so PcbType is actually P150 */
	/* eth will be all disabled as per P100 specs anyways */
	case BOARDTYPE_P100A:
	case BOARDTYPE_P150:
	case BOARDTYPE_P150A:
	case BOARDTYPE_P150C:
		pcb_type = PcbTypeP150;
		break;
	case BOARDTYPE_P300:
	case BOARDTYPE_P300A:
	case BOARDTYPE_P300C:
		pcb_type = PcbTypeP300;
		break;
	case BOARDTYPE_UBB:
		pcb_type = PcbTypeUBB;
		break;
	default:
		pcb_type = PcbTypeUnknown;
		break;
	}

	return pcb_type;
}

/* Reads GPIO6 to determine whether it is p300 left chip. GPIO6 is only set on p300 left chip. */
bool tt_bh_fwtable_is_p300_left_chip(void)
{
	return FIELD_GET(BIT(6), sys_read32(RESET_UNIT_STRAP_REGISTERS_L_REG_ADDR));
}

uint32_t tt_bh_fwtable_get_asic_location(const struct device *dev)
{
	struct bh_fwtable_data *data = dev->data;

	if (!device_is_ready(dev)) {
		LOG_DBG("device is not ready");
		return 0;
	}

	if (tt_bh_fwtable_get_pcb_type(dev) == PcbTypeUBB) {
		return data->read_only_table.asic_location;
	}

	/* Single chip and p300 right are location 0. */
	return tt_bh_fwtable_is_p300_left_chip();
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

	uint8_t buffer[96];
	size_t bytes_read = 0;
	struct bh_fwtable_data *data = dev->data;
	const struct bh_fwtable_config *config = dev->config;
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

	__ASSERT_NO_MSG(table < ARRAY_SIZE(loadcfg));

	tt_boot_fs_fd fd_data;
	int result =
		tt_boot_fs_find_fd_by_tag(config->flash, (uint8_t *)loadcfg[table].tag, &fd_data);
	if (result != TT_BOOT_FS_OK) {
		LOG_ERR("%s() failed with error code %d", loadcfg[table].tag, result);
		return -EIO;
	}

	bytes_read = fd_data.flags.f.image_size;

	flash_read(config->flash, fd_data.spi_addr, buffer, bytes_read);
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
	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		return tt_bh_fwtable_load(dev, BH_FWTABLE_BOARDCFG);
	} else {
		return (tt_bh_fwtable_load(dev, BH_FWTABLE_FLSHINFO) ||
			tt_bh_fwtable_load(dev, BH_FWTABLE_BOARDCFG) ||
			tt_bh_fwtable_load(dev, BH_FWTABLE_CMFWCFG));
	}

	return 0;
}

#define DEFINE_BH_FWTABLE(_inst)                                                                   \
	static struct bh_fwtable_data bh_fwtable_data_##_inst;                                     \
	static const struct bh_fwtable_config bh_fwtable_config_##_inst = {                        \
		.flash = DEVICE_DT_GET(DT_INST_PHANDLE(_inst, flash_dev)),                         \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(_inst, tt_bh_fwtable_init, NULL, &bh_fwtable_data_##_inst,           \
			      &bh_fwtable_config_##_inst, POST_KERNEL,                             \
			      CONFIG_BH_FWTABLE_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_BH_FWTABLE)
