netdir                     = $(includedir)/colibri/net
net_HEADERS                = net.h net_internal.h

SUBDIRS                    = . bulk_emulation lnet

noinst_LTLIBRARIES         = libcolibri-net.la
libcolibri_net_la_SOURCES  = net.c net.h domain.c ep.c buf.c tm.c \
                             $(OTW_U_SOURCES) buffer_pool.c       \
			     buffer_pool.h net_internal.h tm_provision.c

INCLUDES                   = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

OTW_U_SOURCES              = net_otw_types_u.h net_otw_types_u.c
MOSTLYCLEANFILES           = $(OTW_U_SOURCES) net_otw_types_k.h net_otw_types_k.c

EXTRA_DIST                 = net_otw_types.ff

net.h net.c : $(OTW_U_SOURCES)
tm_provision.c: net.h
$(OTW_U_SOURCES): net_otw_types.ff \
    $(top_builddir)/fop/rt/libc2rt.la $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -uk $<
