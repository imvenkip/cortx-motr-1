noinst_PROGRAMS = xcode_test xcode_fop_test

xcode_test_SOURCES = xcode_test.c
xcode_test_LDADD = $(top_builddir)/colibri/libcolibri.la

test_u.h test_u.c : test.ff \
	$(top_builddir)/fop/rt/libc2rt.la
	$(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -u $<

xcode_fop_test_SOURCES = test_u.h test_u.c xcode_fop_test.c
xcode_fop_test_LDADD = $(top_builddir)/colibri/libcolibri.la
