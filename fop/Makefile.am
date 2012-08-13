fopdir                        = $(includedir)/colibri/fop
noinst_LTLIBRARIES            = libcolibri-fop.la

FOM_GENERIC_FOPS              = fom_generic_xc.h fom_generic_xc.c

$(FOM_GENERIC_FOPS): fom_generic_xc.ff  \
	$(top_builddir)/xcode/ff2c/ff2c
	$(top_builddir)/xcode/ff2c/ff2c $<

fop_HEADERS                   = fop.h fom.h fop_item_type.h fom_generic.h

fop_SOURCES                   = fop.c fop.h fom.c fom.h fop_item_type.c \
				fop_item_type.h $(FOM_GENERIC_FOPS)     \
				fom_generic.c fom_long_lock.h fom_long_lock.c

libcolibri_fop_la_SOURCES     = $(fop_SOURCES)

INCLUDES                      = -iquote . -iquote $(top_srcdir)         \
				-iquote $(top_srcdir)/include
EXTRA_DIST                    = linux_kernel/fop_kernel.h

MOSTLYCLEANFILES              = $(FOM_GENERIC_FOPS)
