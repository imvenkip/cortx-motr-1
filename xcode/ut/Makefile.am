noinst_PROGRAMS = xcode_test
noinst_LTLIBRARIES   = libxcode-ut.la

xcode_test_SOURCES = xcode_test.c
xcode_test_LDADD = $(top_builddir)/colibri/libcolibri.la

libxcode_ut_la_SOURCES  = test_u.h test_u.c xcode_fop_test.c

test_u.h test_u.c : test.ff \
	$(top_builddir)/fop/rt/libc2rt.la
	$(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -u $<

#xcode_fop_test_LDADD = $(top_builddir)/colibri/libcolibri.la
#libxcode_ut_la_LIBADD 	= $(top_builddir)/colibri/libcolibri.la
