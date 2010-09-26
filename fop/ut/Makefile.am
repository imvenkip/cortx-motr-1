INCLUDES           = -I. -I$(top_srcdir)/include -I$(top_srcdir)

noinst_LTLIBRARIES = libfop-ut.la

test_format_u.h test_format_u.c test_format_k.h test_format_k.c: \
                                     test_format.ff \
                                     $(top_builddir)/colibri/libcolibri.la \
                                     $(top_builddir)/fop/fop2c 
	sh $(top_builddir)/fop/fop2c -ku $<

libfop_ut_la_SOURCES = test_format_u.h test_format_u.c fmt_test.c test_format.ff

EXTRA_DIST           = test_format.ff

clean-local:
	cd @SRCDIR@/fop/ut ; \
	rm -fr test_format_[uk].[ch]
