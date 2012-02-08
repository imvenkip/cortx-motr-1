lnetdir                    = $(includedir)/net/lnet
lnet_HEADERS               = lnet_core.h lnet.h lnet_xo.h ulnet_core.h

INCLUDES                   = -I. -I$(top_srcdir) -I$(top_srcdir)/include

noinst_LTLIBRARIES         = libcolibri-lnet.la
libcolibri_lnet_la_SOURCES = lnet_main.c $(lnet_HEADERS)

# these .c files are included in lnet_main.c (do not add to _SOURCES)
EXTRA_DIST = bev_cqueue.c lnet_addb.c lnet_core.c lnet_ep.c lnet_tm.c \
             lnet_xo.c ulnet_core.c
