include $(top_srcdir)/Makefile.ff

INCLUDES             = -iquote . -iquote $(top_srcdir)       \
		       -iquote $(top_srcdir)/include         \
		       -iquote /usr/include

PING_FOPS            = ping_fop_ff.h ping_fop_ff.c

noinst_PROGRAMS     = rpcping

rpcping_SOURCES     = $(PING_FOPS) ping_fop.h ping_fop.c     \
		      ping_fom.h ping_fom.c rpc_ping.c

rpcping_LDADD       = $(top_builddir)/colibri/libcolibri.la  \
		      $(top_builddir)/ut/libcolibri-ut.la

EXTRA_DIST          = ping_fop.ff linux_kernel/rpc_ping.h \
                      linux_kernel/rpcping_kernel.c          \
                      linux_kernel/rpcload.sh

MOSTLYCLEANFILES    = $(PING_FOPS)
