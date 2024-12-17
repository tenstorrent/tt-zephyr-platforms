#!/bin/bash

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

set -e

# Installs git hooks for development
manifest_base=$(west list manifest -f "{abspath}")
hook_dir="${manifest_base}/.git/hooks"


add_hook() {
	if [ -f "${hook_dir}/$1" ]; then
		echo "$1 hook already exists, saving to ${hook_dir}/$1.bak"
		mv "${hook_dir}/$1" "${hook_dir}/$1.bak"
	fi

	ln -s "${manifest_base}/scripts/$1.sh" "${hook_dir}/$1"
	chmod +x "${hook_dir}/$1"
	echo "Installed git $1 hook to ${hook_dir}/$1"
}

add_hook pre-push
add_hook pre-commit

echo "Installed git hooks. To skip these hooks, run git commands with --no-verify"
