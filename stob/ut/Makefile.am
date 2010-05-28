INCLUDES         = -I. -I$(top_srcdir) -I$(top_srcdir)/include \
                   -I@DB4SRC@/build_unix -I @DB4SRC@

noinst_PROGRAMS  = server utadieu client

server_SOURCES   = server.c io_fop_xdr.c
server_LDFLAGS   = @LDFLAGS@ -pthread
server_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/sm/libcolibri-sm.la @DB4LIBS@ @AIO_LIBS@ $(top_builddir)/stob/libcolibri-stob.la $(top_builddir)/net/libcolibri-net.la $(top_builddir)/net/sunrpc/libcolibri-net-sunrpc.la

client_SOURCES   = client.c io_fop_xdr.c
client_LDFLAGS   = @LDFLAGS@ -pthread
client_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/net/libcolibri-net.la $(top_builddir)/net/sunrpc/libcolibri-net-sunrpc.la

utadieu_SOURCES   = adieu.c
utadieu_LDFLAGS   = @LDFLAGS@ -pthread
utadieu_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/sm/libcolibri-sm.la $(top_builddir)/../db4/build_unix/libdb-4.8.la @AIO_LIBS@ $(top_builddir)/stob/libcolibri-stob.la
