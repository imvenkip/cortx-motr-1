netdir                     = $(includedir)/net
net_HEADERS                = net.h net_otw_types_u.h net_otw_types_k.h xdr.h

SUBDIRS                    = . usunrpc

noinst_LTLIBRARIES         = libcolibri-net.la
libcolibri_net_la_SOURCES  = net.c net.h domain.c ep.c buf.c tm.c \
                             $(OTW_U_SOURCES) net_xdr.c xdr.h \
                             net_utils.c net_cli.c net_srv.c connection.c
INCLUDES                   = -I. -I$(top_srcdir) -I$(top_srcdir)/include

OTW_U_SOURCES              = net_otw_types_u.h net_otw_types_u.c
#OTW_K_SOURCES              = net_otw_types_k.h net_otw_types_k.c
MOSTLYCLEANFILES           = $(OTW_U_SOURCES) net_otw_types_k.h net_otw_types_k.c

net.c domain.c ep.c net_utils.c net_cli.c net_srv.c connection.c: net.h
net.h net_xdr.c: $(OTW_U_SOURCES) $(OTW_K_SOURCES)
$(OTW_U_SOURCES) $(OTW_K_SOURCES): net_otw_types.ff \
    $(top_builddir)/fop/libc2rt.la $(top_builddir)/fop/fop2c 
	$(top_builddir)/fop/fop2c -uk $<

