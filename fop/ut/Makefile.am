INCLUDES           = -I. -I$(top_srcdir)/include -I$(top_srcdir)

noinst_PROGRAMS    = fmt_test

test_format.h test_format.c test_format_k.h: test_format.ff \
                                     $(top_builddir)/fop/fop.h \
                                     $(top_builddir)/fop/fop_format.h \
                                     $(top_builddir)/fop/fop.c \
                                     $(top_builddir)/fop/fop_format.c \
                                     $(top_builddir)/fop/fop_format_c.c \
                                     $(top_builddir)/fop/fop2c 
	$(top_builddir)/fop/fop2c -ku $<

fmt_test_SOURCES   = test_format.h test_format.c test_format_k.h \
                     fmt_test.c
fmt_test_LDFLAGS   = -pthread
fmt_test_LDADD     = $(top_builddir)/lib/libc2.la \
                     $(top_builddir)/fop/libcolibri-fop.la

EXTRA_DIST	   = test_format.ff

clean-local:
	cd @SRCDIR@/fop/ut ; \
	rm -fr test_format.h test_format.c test_format_k.h
