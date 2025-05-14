AUTOMAKE_OPTIONS = subdir-objects
SUBDIRS=../../../modules/libjaylink
bin_PROGRAMS = tt-console
tt_console_SOURCES = console.c
tt_console_CPPFLAGS = -g -O0 -std=gnu11 -I ../../include -Wall -Werror
noinst_LTLIBRARIES = libmemdriver.la
libmemdriver_la_LIBADD = ../../../modules/libjaylink/libjaylink/libjaylink.la
libmemdriver_la_CPPFLAGS = -I ../../../modules/libjaylink/ -g -O0 -std=gnu11 -I ../../include -Wall -Werror
libmemdriver_la_SOURCES = arc_jtag.c arc_tlb.c

tt_console_LDADD = libmemdriver.la
