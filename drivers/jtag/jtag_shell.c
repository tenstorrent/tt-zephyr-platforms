/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/drivers/jtag.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

/* arbitrary limit is arbitrary */
#define ARBITRARY_LIMIT 16

#define JTAG_CTRL_LIST_ENTRY(node_id) DEVICE_DT_GET(node_id),

#define IS_JTAG_CTRL_LIST(node_id)                                                                 \
	COND_CODE_1(DT_PROP(node_id, jtag_controller), (JTAG_CTRL_LIST_ENTRY(node_id)), ())

static const struct device *jtag_list[] = {DT_FOREACH_STATUS_OKAY_NODE(IS_JTAG_CTRL_LIST)};

static void device_name_get(size_t idx, struct shell_static_entry *entry)
{
	if (idx >= ARRAY_SIZE(jtag_list)) {
		entry->syntax = NULL;
		return;
	}

	entry->syntax = jtag_list[idx]->name;
	entry->handler = NULL;
	entry->help = "Device";
	entry->subcmd = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(sub_jtag_dev, device_name_get);

static const struct device *get_jtag_dev(char *name)
{
	const struct device *dev = device_get_binding(name);
	size_t i;

	for (i = 0; i < ARRAY_SIZE(jtag_list); i++) {
		if (jtag_list[i] == dev) {
			return jtag_list[i];
		}
	}
	return NULL;
}

static int cmd_jtag_tick(const struct shell *sh, size_t argc, char **argv, void *data)
{
	uint32_t count = 1;
	const struct device *dev = get_jtag_dev(argv[2]);

	if (argc >= 3) {
		count = strtoul(argv[2], NULL, 0);
	}

	jtag_tick(dev, count);

	return 0;
}

static int cmd_jtag_read_id(const struct shell *sh, size_t argc, char **argv, void *data)
{
	uint32_t id = 0;
	const struct device *dev = get_jtag_dev(argv[2]);

	jtag_read_id(dev, &id);

	shell_print(sh, "ID: 0x%08x", id);

	return 0;
}

static int cmd_jtag_reset(const struct shell *sh, size_t argc, char **argv, void *data)
{
	const struct device *dev = get_jtag_dev(argv[2]);

	jtag_reset(dev);

	return 0;
}

static int cmd_jtag_ir(const struct shell *sh, size_t argc, char **argv, void *data)
{
	uint32_t count = 0;
	uint32_t data_in[ARBITRARY_LIMIT];
	const struct device *dev = get_jtag_dev(argv[2]);

	count = MIN(argc - 2, ARRAY_SIZE(data_in));
	for (uint32_t i = 0; i < count; i++) {
		data_in[i] = strtoul(argv[i + 2], NULL, 0);
	}

	jtag_update_ir(dev, count, (uint8_t *)data_in);

	return 0;
}

static int cmd_jtag_dr(const struct shell *sh, size_t argc, char **argv, void *data)
{
	bool idle = false;
	uint32_t count = 0;
	uint32_t data_in[ARBITRARY_LIMIT];
	uint32_t data_out[ARBITRARY_LIMIT] = {0};
	const struct device *dev = get_jtag_dev(argv[2]);

	idle = strtoul(argv[2], NULL, 0);

	count = MIN(argc - 3, ARRAY_SIZE(data_in));
	for (uint32_t i = 0; i < count; i++) {
		data_in[i] = strtoul(argv[i + 2], NULL, 0);
	}

	jtag_update_dr(dev, idle, count, (uint8_t *)data_in, (uint8_t *)data_out);

	shell_hexdump_line(sh, 0, (uint8_t *)data_out, count * sizeof(uint32_t));

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_jtag,
	SHELL_CMD_ARG(tick, &sub_jtag_dev,
		      "Clock JTAG TCK pin\n"
		      "Usage: jtag tick <device> [count]\n"
		      "[count] - number of cycles (default 1)\n",
		      cmd_jtag_tick, 2, 1),
	SHELL_CMD_ARG(read_id, &sub_jtag_dev,
		      "Read JTAG target id\n"
		      "Usage: jtag read_id <device>",
		      cmd_jtag_read_id, 2, 0),
	SHELL_CMD_ARG(reset, &sub_jtag_dev,
		      "Reset JTAG target\n"
		      "Usage: jtag reset <device>",
		      cmd_jtag_reset, 2, 0),
	SHELL_CMD_ARG(ir, &sub_jtag_dev,
		      "Update JTAG IR\n"
		      "Usage: jtag ir <device> [<word0> <word1> ..]\n"
		      "<word0> - 32-bit word (decimal or hex)",
		      cmd_jtag_ir, 2, ARBITRARY_LIMIT),
	SHELL_CMD_ARG(dr, &sub_jtag_dev,
		      "Update JTAG DR\n"
		      "Usage: jtag dr [device] <idle> [<word0> <word1> ..]\n"
		      "<idle> - a non-zero integer to set the device back to idle\n"
		      "<word0> - 32-bit word (decimal or hex)",
		      cmd_jtag_dr, 3, ARBITRARY_LIMIT),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_REGISTER(jtag, &sub_jtag, "JTAG commands", NULL);
