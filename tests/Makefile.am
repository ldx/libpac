ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = -I$(top_builddir) -I$(top_srcdir) \
			  -I$(top_builddir)/include -I$(top_srcdir)/include \
			  @LOCAL_CPPFLAGS@ $(EXTRA_CPPFLAGS)
AM_LDFLAGS = @LOCAL_LDFLAGS@ $(EXTRA_LDFLAGS)

LIBS += $(EXTRA_LIBS) ../libpac.la

check_PROGRAMS = test_unit1

noinst_PROGRAMS = test_pac
test_pac_SOURCES = test_pac.c
test_pac_CPPFLAGS = $(AM_CPPFLAGS)

TESTS = test_unit1 test1.sh test2.sh test3.sh

test_unit1_SOURCES = test_unit1.c
