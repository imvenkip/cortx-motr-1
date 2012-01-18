rpcpingdir	     = $(includedir)/rpc/it
INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I/usr/include

ping_fop_u.h ping_fop_u.c ping_fop_k.h ping_fop_k.c: ping_fop.ff \
		$(top_builddir)/fop/rt/libc2rt.la \
		$(top_builddir)/fop/fop2c
		$(top_builddir)/fop/fop2c -ku $<

noinst_PROGRAMS     = rpcping

rpcping_SOURCES     = ping_fop_u.h ping_fop_u.c ping_fop.h ping_fop.c \
		      ping_fom.h ping_fom.c rpc_ping.c

rpcping_LDADD       = $(top_builddir)/colibri/libcolibri.la \
		      $(top_builddir)/ut/libcolibri-ut.la

EXTRA_DIST          = ping_fop.ff

clean-local:
	cd $(rpcpingdir) ; \
	rm -fr ping_fop_[uk].[ch]
