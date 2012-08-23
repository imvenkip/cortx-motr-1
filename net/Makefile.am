include $(top_srcdir)/Makefile.ff

netdir                     = $(includedir)/colibri/net
net_HEADERS                = net.h net_internal.h

SUBDIRS                    = . bulk_emulation lnet

noinst_LTLIBRARIES         = libcolibri-net.la
libcolibri_net_la_SOURCES  = net.c net.h domain.c ep.c buf.c tm.c \
                             $(OTW_SOURCES) buffer_pool.c         \
			     buffer_pool.h net_internal.h tm_provision.c

INCLUDES                   = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

OTW_SOURCES                = net_otw_types_ff.h net_otw_types_ff.c
MOSTLYCLEANFILES           = $(OTW_SOURCES)

EXTRA_DIST                 = net_otw_types.ff

net.h net.c : $(OTW_SOURCES)
tm_provision.c: net.h
