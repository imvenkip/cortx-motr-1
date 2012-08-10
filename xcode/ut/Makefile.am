include $(top_srcdir)/Makefile.xc

xcodeutdir        = $(includedir)/xcode/ut
noinst_LTLIBRARIES   = libxcode-ut.la

#xcode_ut_LDADD = $(top_builddir)/colibri/libcolibri.la

libxcode_ut_la_SOURCES  = test_u.h test_u.c xcode_fop_test.c xcode_test.c \
			xcode.c ff2c.c test_gccxml_simple_xc.c test_gccxml_xc.c

test_u.h test_u.c : test.ff \
	$(top_builddir)/fop/rt/libc2rt.la
	$(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -u $<

EXTRA_DIST = test.ff

clean-local: clean-xc
	cd @SRCDIR@/xcode/ut ; \
	rm -fr test_[uk].[ch]
