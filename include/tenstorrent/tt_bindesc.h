/*
 * Copyright (c) 2026 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TENSTORRENT_TT_BINDESC_H_
#define TENSTORRENT_TT_BINDESC_H_

/** The app major.minor.tweak version, plus the git sha of the repo
 * EG v0.30.0-33a7588b
 */
#define BINDESC_ID_APP_GIT_VERSION 0x8FF

#if defined(CONFIG_TT_BINDESC)
extern const struct bindesc_entry BINDESC_NAME(app_git_version);
#endif

#endif /* TENSTORRENT_TT_BINDESC_H_ */
