lnetdir                    = $(includedir)/net/lnet
lnet_HEADERS               = lnet_core.h lnet.h lnet_xo.h ulnet_core.h

INCLUDES                   = -I. -I$(top_srcdir) -I$(top_srcdir)/include

noinst_LTLIBRARIES         = libcolibri-lnet.la
libcolibri_lnet_la_SOURCES = lnet_main.c $(lnet_HEADERS)

# depend on included .c files (do not add to _SOURCES)
lnet_main.c: bev_cqueue.c ulnet_core.c lnet_xo.c
lnet_main.c: lnet_core.h
