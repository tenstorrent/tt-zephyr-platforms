# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

# Makefile for tooling scripts that need direct TLB access.

TOOLS = tt-console

tools: $(TOOLS)

CFLAGS += -O0 -g -Wall -Wextra -Werror -std=gnu11 -I ../../include

tt-console: console.c vuart.c
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f $(TOOLS)
