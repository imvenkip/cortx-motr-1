include $(top_srcdir)/Makefile.ff

noinst_LTLIBRARIES   = libfop-ut.la
INCLUDES             = -iquote . -iquote $(top_srcdir)/include -iquote $(top_srcdir)

ITERATOR_FOPS        = iterator_test_ff.h iterator_test_ff.c

libfop_ut_la_SOURCES = $(ITERATOR_FOPS) iterator_test.c

EXTRA_DIST           = iterator_test.ff

MOSTLYCLEANFILES     = $(ITERATOR_FOPS)
