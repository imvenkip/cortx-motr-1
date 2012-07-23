noinst_LTLIBRARIES   = libfop-ut.la
INCLUDES             = -iquote . -iquote $(top_srcdir)/include -iquote $(top_srcdir)

ITERATOR_FOPS        = iterator_test_xc.h iterator_test_xc.c

$(ITERATOR_FOPS) : iterator_test_xc.ff
	$(top_builddir)/xcode/ff2c/ff2c $<

libfop_ut_la_SOURCES = $(ITERATOR_FOPS) iterator_test.c

EXTRA_DIST           = iterator_test_xc.ff

MOSTLYCLEANFILES     = $(ITERATOR_FOPS)
