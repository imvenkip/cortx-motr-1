netdir                     = $(includedir)/colibri/net
net_HEADERS                = net.h xdr.h net_internal.h

SUBDIRS                    = . usunrpc bulk_emulation

noinst_LTLIBRARIES         = libcolibri-net.la
libcolibri_net_la_SOURCES  = net.c net.h domain.c ep.c buf.c tm.c \
                             $(OTW_U_SOURCES) net_xdr.c xdr.h \
                             net_utils.c net_cli.c net_srv.c connection.c \
			     buffer_pool.c buffer_pool.h net_internal.h
INCLUDES                   = -I. -I$(top_srcdir) -I$(top_srcdir)/include

OTW_U_SOURCES              = net_otw_types_u.h net_otw_types_u.c
MOSTLYCLEANFILES           = $(OTW_U_SOURCES) net_otw_types_k.h net_otw_types_k.c

EXTRA_DIST                 = net_otw_types.ff \
                             ksunrpc/ksunrpc.h \
                             ksunrpc/client.c \
                             ksunrpc/domain.c \
                             ksunrpc/kxdr.c

net.c domain.c ep.c net_utils.c net_cli.c net_srv.c connection.c: net.h
net.h net_xdr.c: $(OTW_U_SOURCES)
$(OTW_U_SOURCES): net_otw_types.ff \
    $(top_builddir)/fop/rt/libc2rt.la $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -uk $<

dist-hook:
	mkdir -p @top_builddir@/@PACKAGE@-@VERSION@/net/ksunrpc
	cp @SRCDIR@/net/ksunrpc/ksunrpc.h @top_builddir@/@PACKAGE@-@VERSION@/net/ksunrpc
