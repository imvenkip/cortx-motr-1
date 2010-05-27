INCLUDES          = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I@DB4SRC@/build_unix

noinst_PROGRAMS   = rclient rserver

rclient_SOURCES   = rpc-client.c
rclient_CFLAGS    = -pthread
rclient_LDFLAGS   = @LDFLAGS@
rclient_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/rpc/librpc.la \
		    $(top_builddir)/net/libcolibri-net.la $(top_builddir)/net/sunrpc/libcolibri-net-sunrpc.la

rserver_SOURCES   = rpc-server.c
rserver_CFLAGS    = -pthread
rserver_LDFLAGS   = @LDFLAGS@
rserver_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/rpc/librpc.la \
		    $(top_builddir)/net/libcolibri-net.la $(top_builddir)/net/sunrpc/libcolibri-net-sunrpc.la @DB4LIBS@


