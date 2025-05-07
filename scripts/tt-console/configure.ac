AC_DEFUN([AX_CONFIG_SUBDIR_OPTION],
[
AC_CONFIG_SUBDIRS([$1])

m4_ifblank([$2], [rm -f $srcdir/$1/configure.gnu],
[printf '#!/bin/sh\nexec "`dirname "'\$'0"`/configure" '"$2"' "'\$'@"\n' > "$srcdir/$1/configure.gnu"
])
])

AC_INIT([tt-console], [1.0])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AM_PROG_AR
AC_PROG_CC
LT_INIT
AX_CONFIG_SUBDIR_OPTION([../../../modules/libjaylink/],
		[--enable-subproject-build])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
 Makefile
])
AC_OUTPUT
