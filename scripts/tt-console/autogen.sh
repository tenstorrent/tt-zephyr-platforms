#
# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: GPL-2.0-only
#

(cd ../../../modules/libjaylink; ./autogen.sh)
libtoolize --automake --copy
aclocal
autoheader
autoconf
automake --add-missing
