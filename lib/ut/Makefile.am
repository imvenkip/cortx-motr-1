noinst_PROGRAMS   = utlibc2

utlibc2_SOURCES   = main.c list.c memory.c bitops.c bitmap.c refs.c queue.c \
                    vec.c thread.c mutex.c chan.c cache.c atomic.c
utlibc2_LDFLAGS   = -pthread
utlibc2_LDADD     = $(top_builddir)/lib/libc2.la @PTHREAD_LIBS@
INCLUDES          = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I @DBSRC@/build_unix
