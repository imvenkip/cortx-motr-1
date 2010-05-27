INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include

noinst_PROGRAMS = connection client kclient
# utils
# client server 

connection_SOURCES   = connection.c
connection_LDFLAGS   = @LDFLAGS@
connection_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/net/libcolibri-net.la $(top_builddir)/net/sunrpc/libcolibri-net-sunrpc.la @DB4LIBS@

#utils_SOURCES   = connection.c
#utils_LDFLAGS   = @LDFLAGS@
#utils_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/net/libcolibri-net.la $(top_builddir)/net/sunrpc/libcolibri-net-sunrpc.la @DB4LIBS@


client_SOURCES   = client.c
client_LDFLAGS   = @LDFLAGS@
client_CFLAGS    = -pthread
client_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/net/libcolibri-net.la $(top_builddir)/net/sunrpc/libcolibri-net-sunrpc.la @DB4LIBS@

#rpc-server_SOURCES   = rpc-server.c
#rpc-server_LDFLAGS   = @LDFLAGS@
#rpc-server_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/net/libcolibri-net.la $(top_builddir)/net/sunrpc/libcolibri-net-sunrpc.la @DB4LIBS@


kclient_SOURCES   = kclient.c
kclient_LDFLAGS   = @LDFLAGS@
kclient_LDADD     =
