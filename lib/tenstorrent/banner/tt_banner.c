/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <version.h>

static int tt_boot_banner(void)
{
	/* clang-format off */
	static const char *const logo =
"         .:.                 .:\n"
"      .:-----:..             :+++-.\n"
"   .:------------:.          :++++++=:\n"
" :------------------:..      :+++++++++\n"
" :----------------------:.   :+++++++++\n"
" :-------------------------:.:+++++++++\n"
" :--------:  .:-----------:. :+++++++++\n"
" :--------:     .:----:.     :+++++++++\n"
" .:-------:         .        :++++++++-\n"
"    .:----:                  :++++=:.\n"
"        .::                  :+=:\n"
"          .:.               ::\n"
"          .===-:        .-===-\n"
"          .=======:. :-======-\n"
"          .==================-\n"
"          .==================-\n"
"           ==================:\n"
"            :-==========-:.\n"
"                .:====-.\n"
"\n";
	/* clang-format on */

	printk(logo);
	printk("*** Booting " CONFIG_BOARD " with Zephyr OS " STRINGIFY(BUILD_VERSION) " ***\n");

	return 0;
}

SYS_INIT(tt_boot_banner, POST_KERNEL, 0);
