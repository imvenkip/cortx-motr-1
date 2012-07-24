fopdir                        = $(includedir)/colibri/fop
noinst_LTLIBRARIES            = libcolibri-fop.la

fop_HEADERS                   = fop.h fop_base.h fom.h fop_format.h fop_item_type.h

fop_SOURCES                   = fop.c fop.h fom.c fom.h fop_format.h fop_item_type.c \
				fop_item_type.h fop_base.c fop_base.h

libcolibri_fop_la_SOURCES     = $(fop_SOURCES)

INCLUDES                      = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include
EXTRA_DIST                    = fop2c.in linux_kernel/fop_kernel.h
