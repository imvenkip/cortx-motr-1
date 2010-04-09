INCLUDES             = -I. -I$(top_srcdir)/include -I$(top_srcdir)

noinst_PROGRAMS = rpc-client rpc-server

rpc-client_SOURCES   = rpc-server.c
rpc-client_LDFLAGS   = @LDFLAGS@
rpc-client_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/rpc/librpc.la




