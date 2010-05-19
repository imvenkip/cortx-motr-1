INCLUDES             = -I. -I$(top_srcdir)/include -I$(top_srcdir)  -I@DB4SRC@/build_unix

noinst_PROGRAMS = rclient rserver

rclient_SOURCES   = rpc-client.c
rclient_LDFLAGS   = @LDFLAGS@
rclient_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/rpc/librpc.la \
		    $(top_builddir)/net/libcolibri-net.la

rserver_SOURCES   = rpc-server.c
rserver_LDFLAGS   = @LDFLAGS@
rserver_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/rpc/librpc.la \
		    $(top_builddir)/net/libcolibri-net.la $(DB4LIBS)


