INCLUDES                   = -I. -I$(top_srcdir)/include -I$(top_srcdir)

noinst_PROGRAMS  = fmt_test

fmt_test_SOURCES   = fmt_test.c
fmt_test_LDFLAGS   = @LDFLAGS@ -pthread
fmt_test_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/fop/libcolibri-fop.la

