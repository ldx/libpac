ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = -I$(top_builddir) -I$(top_srcdir) \
			  -I$(top_builddir)/include -I$(top_srcdir)/include \
			  @LOCAL_CPPFLAGS@ $(EXTRA_CPPFLAGS)
AM_LDFLAGS = @LOCAL_LDFLAGS@ $(EXTRA_LDFLAGS)

LIBS += $(EXTRA_LIBS)

SUBDIRS = .

include_HEADERS = pac.h

pkgconfigdir=$(libdir)/pkgconfig

LIBRARY_VERSION = 0:0:0

SOURCES = duktape.c pac.c threadpool.c util.c

lib_LTLIBRARIES = libpac.la
libpac_la_SOURCES = $(SOURCES)
libpac_la_LDFLAGS = -version-info $(LIBRARY_VERSION)

bin_PROGRAMS = example
example_SOURCES = example.c
example_CPPFLAGS = $(AM_CPPFLAGS)
example_LDADD = libpac.la
