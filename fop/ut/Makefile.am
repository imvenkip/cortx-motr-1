noinst_LTLIBRARIES = libfop-ut.la
INCLUDES           = -I. -I$(top_srcdir)/include -I$(top_srcdir)

test_format_u.h test_format_u.c test_format_k.h test_format_k.c: \
                                     test_format.ff \
                                     $(top_builddir)/fop/rt/libc2rt.la \
                                     $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -ku $<

iterator_test_u.h iterator_test_u.c: \
                                     iterator_test.ff \
                                     $(top_builddir)/colibri/libcolibri.la \
                                     $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -u $<

libfop_ut_la_SOURCES = iterator_test_u.h iterator_test_u.c \
			test_format_u.h test_format_u.c \
			fmt_test.c test_format.ff \
			iterator_test.c

EXTRA_DIST = test_format.ff iterator_test.ff

clean-local:
	cd @SRCDIR@/fop/ut ; \
	rm -fr test_format_[uk].[ch] iterator_test_u.[ch]
