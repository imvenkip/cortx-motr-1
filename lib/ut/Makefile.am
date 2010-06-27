noinst_PROGRAMS   = utlibc2

utlibc2_SOURCES   = main.c list.c memory.c refs.c queue.c \
                    vec.c thread.c mutex.c chan.c cache.c atomic.c
utlibc2_LDFLAGS   = -pthread
utlibc2_LDADD     = $(top_builddir)/colibri/libcolibri.la
INCLUDES          = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I @DBSRC@/build_unix
