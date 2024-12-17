# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bh_arc_mqueue)

target_sources(app PRIVATE
  src/main.c
  ../../../../lib/tenstorrent/bh_arc/msgqueue.c
  ../../../../lib/tenstorrent/bh_arc/post_code.c
)

target_compile_definitions(app PRIVATE MSG_QUEUE_TEST)
