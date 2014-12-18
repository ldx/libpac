#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_INIT([libpac], [0.0.0], [v.nebehaj@gmail.com])
AC_CONFIG_SRCDIR([pac.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

PKG_PROG_PKG_CONFIG

AM_INIT_AUTOMAKE([foreign subdir-objects])

LT_INIT

# Checks for programs.
AC_PROG_CC
AC_PROG_LIBTOOL

AC_MSG_CHECKING(for WIN32)
AC_TRY_COMPILE(,
	[
#ifndef WIN32
die horribly
#endif
	],
	bwin32=true; AC_MSG_RESULT(yes),
	bwin32=false; AC_MSG_RESULT(no),
)
AM_CONDITIONAL(BUILD_WIN32, test x$bwin32 = xtrue)

AC_ARG_ENABLE([deep-c-stack],
              [  --enable-deep-c-stack build with deep C stack enabled],
			  [deep_c_stack=true], [deep_c_stack=false])

LOCAL_CPPFLAGS="-std=c99 -pedantic -Wall -O2 -g"
if test x$deep_c_stack = xtrue; then
    LOCAL_CPPFLAGS="$LOCAL_CPPFLAGS -DDUK_OPT_DEEP_C_STACK"
fi
LOCAL_LDFLAGS=""
if test "$bwin32" = true; then
    LOCAL_CPPFLAGS="$LOCAL_CPPFLAGS -mno-ms-bitfields -D_WIN32_WINNT=0x0600"
else
    LOCAL_CPPFLAGS="$LOCAL_CPPFLAGS -D_GNU_SOURCE -D_POSIX_C_SOURCE -D_DARWIN_C_SOURCE -D_XOPEN_SOURCE"
    LOCAL_LDFLAGS="$LOCAL_LDFLAGS -rdynamic"
fi

# -lm
AC_SEARCH_LIBS([cos], [m], [], [AC_MSG_ERROR([not found])])

# -lpthread
AC_SEARCH_LIBS([pthread_create], [pthread], [], [AC_MSG_ERROR([not found])])
AC_CHECK_HEADER([pthread.h], [], [AC_MSG_ERROR([not found])])

if test "$bwin32" = true; then
    AC_CHECK_LIB(iphlpapi, main)
    AC_CHECK_LIB(kernel32, main)
    AC_CHECK_LIB(ws2_32, main)
    AC_CHECK_LIB(gdi32, main)
    AC_CHECK_LIB(dbghelp, main)
fi

AC_SUBST(LOCAL_CPPFLAGS)
AC_SUBST(LOCAL_LDFLAGS)

AC_CONFIG_FILES([Makefile tests/Makefile])
AC_OUTPUT
