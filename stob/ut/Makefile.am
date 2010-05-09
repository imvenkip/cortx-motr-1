INCLUDES         = -I. -I$(top_srcdir) -I$(top_srcdir)/include  -I@DB4SRC@ -I@DB4SRC@/build_unix
noinst_PROGRAMS  = server
server_SOURCES   = server.c
server_LDFLAGS   = @LDFLAGS@ -pthread
server_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/sm/libcolibri-sm.la @DB4LIBS@ @AIO_LIBS@ $(top_builddir)/stob/libcolibri-stob.la
