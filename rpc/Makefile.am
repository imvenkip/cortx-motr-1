SUBDIRS            = . ut

lib_LTLIBRARIES    = librpc.la
librpc_la_SOURCES  = pcache.c pcache.h rpclib.c rpclib.h session_svc.c session_svc.h \
                     xdr/common.c xdr/common.h xdr/session.c xdr/session.h
librpc_la_LDFLAGS  = -release @LT_RELEASE@
librpc_la_LIBADD   = $(top_builddir)/lib/libc2.la @LIBS@ @PTHREAD_LIBS@ @DBLIBS@

INCLUDES           = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I@DBSRC@/build_unix
EXTRA_DIST         = compound.c compound_cli.h compound_srv.h compound_types.h rpc_ops.h \
                     rpc_types.h session.h session_cli.c session_cli.h session_srv.c \
                     session_srv.h session_svc.c session_svc.h session_types.h slot.h
