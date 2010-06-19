INCLUDES         = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I@DBSRC@/build_unix -I @DBSRC@

if LINUX
noinst_PROGRAMS  = server utadieu client

server_SOURCES   = server.c io_fop_xdr.c io_fop.h
server_LDFLAGS   = -pthread
server_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/sm/libcolibri-sm.la @AIO_LIBS@ $(top_builddir)/stob/libcolibri-stob.la $(top_builddir)/net/libcolibri-net.la $(top_builddir)/net/usunrpc/libcolibri-net-usunrpc.la $(top_builddir)/addb/libcolibri-addb.la

client_SOURCES   = client.c io_fop_xdr.c io_fop.h
client_LDFLAGS   = -pthread
client_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/net/libcolibri-net.la $(top_builddir)/net/usunrpc/libcolibri-net-usunrpc.la $(top_builddir)/addb/libcolibri-addb.la

utadieu_SOURCES  = adieu.c
utadieu_LDFLAGS  = -pthread
utadieu_LDADD    = $(top_builddir)/lib/libc2.la $(top_builddir)/sm/libcolibri-sm.la @AIO_LIBS@ $(top_builddir)/stob/libcolibri-stob.la $(top_builddir)/addb/libcolibri-addb.la
endif
