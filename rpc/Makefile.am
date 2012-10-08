include $(top_srcdir)/Makefile.xc
include $(top_srcdir)/Makefile.ff

rpcdir			   = $(includedir)/colibri/rpc
rpc_HEADERS		   = session.h session_fops.h session_foms.h \
			     session_internal.h rpc_opcodes.h service.h \
			     rpc_helpers.h

SESSION_FOPS               = session_ff.h session_ff.c

noinst_LTLIBRARIES	   = libcolibri-rpc.la

libcolibri_rpc_la_SOURCES  = $(SESSION_FOPS) rpc_onwire_xc.h rpc_onwire_xc.c \
			     session.c session.h \
			     session_internal.h slot.c conn.c \
			     session_utils.c \
			     session_fops.c session_fops.h \
			     session_foms.c session_foms.h \
			     rpc2.c rpc2.h item.h item.c \
			     rpc_machine.h rpc_machine.c \
			     packet.c packet.h \
			     bulk.h bulk.c \
			     formation2.h formation2.c frmops.c \
			     rpc_onwire.c rpc_onwire.h \
			     rpclib.c rpclib.h rpc_opcodes.h service.c

INCLUDES		  = -iquote . -iquote $(top_srcdir) \
			    -iquote $(top_srcdir)/include \
			    -iquote /usr/include

EXTRA_DIST                = session.ff

MOSTLYCLEANFILES          = $(SESSION_FOPS)

clean-local: clean-xc
