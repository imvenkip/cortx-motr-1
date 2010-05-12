SUBDIRS    = . ut

lib_LTLIBRARIES = librpc.la
librpc_la_SOURCES  = session_cli.c session_srv.c rpc_lib.c session_svc.c compound.c xdr/session_xdr.c
librpc_la_LDFLAGS  = -release @LT_RELEASE@
librpc_la_LIBADD   = $(top_builddir)/lib/libc2.la @LIBS@

INCLUDES           = -I. -I$(top_srcdir)/include -I$(top_srcdir)
