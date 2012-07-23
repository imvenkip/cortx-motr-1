rpcdir			   = $(includedir)/colibri/rpc
rpc_HEADERS		   = session.h session_fops.h session_foms.h session_internal.h rpc_opcodes.h \
			     service.h

SESSION_FOPS               = session_xc.h session_xc.c

$(SESSION_FOPS) : session_xc.ff
	$(top_builddir)/xcode/ff2c/ff2c $<

noinst_LTLIBRARIES	   = libcolibri-rpc.la

libcolibri_rpc_la_SOURCES  = $(SESSION_FOPS) session.c session.h  \
			     session_internal.h slot.c conn.c     \
			     session_utils.c                      \
			     session_fops.c session_fops.h        \
			     session_foms.c session_foms.h        \
			     rpc2.c rpc2.h item.h item.c rpcdbg.h \
			     rpc_machine.h rpc_machine.c          \
			     packet.c packet.h                    \
			     bulk.h bulk.c                        \
			     formation.h formation.c              \
			     formation2.h formation2_internal.h   \
			     formation2.c frmops.c                \
			     rpc_onwire.c rpc_onwire.h            \
			     rpclib.c rpclib.h rpc_opcodes.h      \
			     service.c


INCLUDES		  = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include -iquote /usr/include
EXTRA_DIST                = session_xc.ff

MOSTLYCLEANFILES          = $(SESSION_FOPS)
