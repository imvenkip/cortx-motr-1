rpcdir			= @SRCDIR@/rpc
rpc_HEADERS		= session.h session_fops.h session_foms.h session_internal.h

session_u.h session_u.c session_k.h session_k.c: session.ff \
				$(top_builddir)/fop/rt/libc2rt.la \
				$(top_builddir)/fop/fop2c
				$(top_builddir)/fop/fop2c -ku $<

noinst_LTLIBRARIES	= libcolibri-rpc.la
libcolibri_rpc_la_SOURCES  = session_u.h session_u.c session.c session.h session_internal.h \
				session_fops.c session_fops.h \
				session_foms.c session_foms.h \
				rpccore.c rpccore.h \
				formation.h formation.c \
				rpc_group.c rpc_onwire.c rpc_onwire.h


INCLUDES		= -I. -I$(top_srcdir) -I$(top_srcdir)/include -I/usr/include
EXTRA_DIST     = session.ff
