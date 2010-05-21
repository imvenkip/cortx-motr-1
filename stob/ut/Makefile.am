INCLUDES                   = -I. -I$(top_srcdir)/include -I@DB4SRC@/build_unix -I @DB4SRC@

noinst_PROGRAMS  = server
server_SOURCES   = server.c
server_LDFLAGS   = @LDFLAGS@ -pthread
server_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/sm/libcolibri-sm.la $(top_builddir)/../db4/build_unix/libdb-4.8.la @AIO_LIBS@ $(top_builddir)/stob/libcolibri-stob.la