INCLUDES           = -I. -I$(top_srcdir)/include -I$(top_srcdir)

noinst_PROGRAMS    = fmt_test

test_format_u.h test_format_u.c test_format_k.h test_format_k.c: \
                                     test_format.ff \
                                     $(top_builddir)/colibri/libcolibri.la \
                                     $(top_builddir)/fop/fop2c 
	$(top_builddir)/fop/fop2c -ku $<

fmt_test_SOURCES   = test_format_u.h test_format_u.c test_format_k.h \
                     test_format_k.c fmt_test.c
fmt_test_LDFLAGS   = -pthread
fmt_test_LDADD     = $(top_builddir)/colibri/libcolibri.la

EXTRA_DIST	   = test_format.ff

clean-local:
	cd @SRCDIR@/fop/ut ; \
	rm -fr test_format_[uk].[ch]
