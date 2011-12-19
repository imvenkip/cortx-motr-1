UT_SRCDIR          = @SRCDIR@/colibri/ut

noinst_LTLIBRARIES = libcs-ut.la
INCLUDES           = -I. -I$(top_srcdir)/include -I$(top_srcdir)

libcs_ut_la_SOURCES = cs_ut_main.c

libcs_ut_la_LIBADD = $(top_builddir)/colibri/libcolibri.la

EXTRA_DIST = cs_test_fops.ff

clean-local:
	rm -f cs_test_fops_u.[ch]
