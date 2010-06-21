INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include

noinst_PROGRAMS      = connection client kclient
# utils
# client server 

connection_SOURCES   = connection.c
connection_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/net/libcolibri-net.la $(top_builddir)/net/usunrpc/libcolibri-net-usunrpc.la $(top_builddir)/addb/libcolibri-addb.la

#utils_SOURCES       = connection.c
#utils_LDADD         = $(top_builddir)/lib/libc2.la $(top_builddir)/net/libcolibri-net.la $(top_builddir)/net/usunrpc/libcolibri-net-usunrpc.la $(top_builddir)/addb/libcolibri-addb.la


client_SOURCES       = client.c
client_LDFLAGS       = -pthread
client_LDADD         = $(top_builddir)/lib/libc2.la $(top_builddir)/net/libcolibri-net.la $(top_builddir)/net/usunrpc/libcolibri-net-usunrpc.la $(top_builddir)/addb/libcolibri-addb.la

#rpc-server_SOURCES  = rpc-server.c
#rpc-server_LDADD    = $(top_builddir)/lib/libc2.la $(top_builddir)/net/libcolibri-net.la $(top_builddir)/net/usunrpc/libcolibri-net-usunrpc.la $(top_builddir)/addb/libcolibri-addb.la

kclient_SOURCES      = kclient.c
kclient_LDADD        =
