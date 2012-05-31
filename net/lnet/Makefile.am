lnetdir                    = $(includedir)/colibri/net/lnet
lnet_HEADERS               = lnet.h

INCLUDES                   = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

noinst_LTLIBRARIES         = libcolibri-lnet.la
libcolibri_lnet_la_SOURCES = lnet_main.c lnet_core.h lnet_core_types.h 	    \
                             lnet_ioctl.h lnet_pvt.h lnet_xo.h ulnet_core.h \
			     lnet_utils.c

# the .c files are included in lnet_main.c (do not add to _SOURCES)
EXTRA_DIST = \
             bev_cqueue.c \
             linux_kernel/kbev_cqueue.c \
             linux_kernel/klnet_core.c \
             linux_kernel/klnet_core.h \
             linux_kernel/klnet_drv.c \
             linux_kernel/klnet_drv.h \
             linux_kernel/klnet_utils.c \
             linux_kernel/klnet_vec.c \
             lnet_addb.c \
             lnet_core.c \
             lnet_ep.c \
             lnet_tm.c \
             lnet_xo.c \
             ulnet_core.c
