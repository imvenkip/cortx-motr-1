netdir                     = $(includedir)/colibri/net
net_HEADERS                = net.h xdr.h net_internal.h

SUBDIRS                    = . bulk_emulation lnet

noinst_LTLIBRARIES         = libcolibri-net.la
libcolibri_net_la_SOURCES  = net.c net.h domain.c ep.c buf.c tm.c \
                             $(OTW_U_SOURCES) net_xdr.c xdr.h \
                             net_utils.c net_cli.c net_srv.c connection.c \
			     buffer_pool.c buffer_pool.h net_internal.h
INCLUDES                   = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

OTW_U_SOURCES              = net_otw_types_u.h net_otw_types_u.c
MOSTLYCLEANFILES           = $(OTW_U_SOURCES) net_otw_types_k.h net_otw_types_k.c

EXTRA_DIST                 = net_otw_types.ff

net.c domain.c ep.c net_utils.c net_cli.c net_srv.c connection.c: net.h
net.h net_xdr.c: $(OTW_U_SOURCES)
$(OTW_U_SOURCES): net_otw_types.ff \
    $(top_builddir)/fop/rt/libc2rt.la $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -uk $<
