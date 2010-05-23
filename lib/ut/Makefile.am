noinst_PROGRAMS   = utlibc2

utlibc2_SOURCES   = main.c list.c memory.c bitops.c bitmap.c refs.c queue.c \
                    vec.c thread.c mutex.c chan.c atomic.c
utlibc2_LDFLAGS   = @LDFLAGS@ -pthread
utlibc2_LDADD     = $(top_builddir)/lib/libc2.la
INCLUDES          = -I. -I$(top_srcdir)/include -I$(top_srcdir)
