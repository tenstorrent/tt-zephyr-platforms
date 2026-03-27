/*
 * Copyright (c) 2026 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/bindesc.h>
#include <zephyr/version.h>
#include <tenstorrent/tt_bindesc.h>

#if defined(HAS_APP_VERSION)
#include <zephyr/app_version.h>

BINDESC_STR_DEFINE(app_git_version, BINDESC_ID_APP_GIT_VERSION, STRINGIFY(APP_GIT_VERSION));

#endif /* defined(HAS_APP_VERSION) */
