INCLUDES             = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include -iquote /usr/include

ping_fop_u.h ping_fop_u.c ping_fop_k.h ping_fop_k.c: ping_fop.ff \
		$(top_builddir)/fop/rt/libc2rt.la \
		$(top_builddir)/fop/fop2c
		$(top_builddir)/fop/fop2c -ku $<

noinst_PROGRAMS     = rpcping

rpcping_SOURCES     = ping_fop_u.h ping_fop_u.c ping_fop.h ping_fop.c \
		      ping_fom.h ping_fom.c rpc_ping.c

rpcping_LDADD       = $(top_builddir)/colibri/libcolibri.la \
		      $(top_builddir)/ut/libcolibri-ut.la

EXTRA_DIST          = ping_fop.ff linux_kernel/rpc_ping.h \
                      linux_kernel/rpcping_kernel.c \
                      linux_kernel/rpcload.sh

clean-local:
	cd @SRCDIR@/rpc/it ; \
	rm -fr ping_fop_[uk].[ch]
