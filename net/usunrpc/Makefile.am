usunrpcdir                         = $(includedir)/colibri/net/usunrpc
usunrpc_HEADERS                    = usunrpc.h usunrpc_internal.h

noinst_LTLIBRARIES                 = libcolibri-net-usunrpc.la
libcolibri_net_usunrpc_la_SOURCES  = client.c server.c domain.c usunrpc.h \
                                     usunrpc_internal.h uxdr.c
INCLUDES                           = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include
