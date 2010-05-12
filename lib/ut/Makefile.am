noinst_PROGRAMS   = utlibc2

utlibc2_SOURCES   = main.c list.c memory.c bitops.c bitmap.c refs.c cache.c
utlibc2_LDFLAGS   = @LDFLAGS@
utlibc2_LDADD     = $(top_builddir)/lib/libc2.la @DB4LIBS@ @PTHREAD_LIBS@
INCLUDES          = -I. -I$(top_srcdir)/include -I$(top_srcdir) -I $(DB4SRC)/build_unix
