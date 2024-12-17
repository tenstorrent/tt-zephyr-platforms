/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef COMPILER_H_INCLUDED
#define COMPILER_H_INCLUDED

#include <stddef.h>

#if __STDC_VERSION__ < 202311L
#define unreachable() (__builtin_unreachable())
#endif

#endif
