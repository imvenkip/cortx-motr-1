fopdir                        = $(includedir)/fop
fop_HEADERS                   = fop.h fop_format.h fop_user.h fop_format_def.h

noinst_LTLIBRARIES            = libcolibri-fop.la
libcolibri_fop_la_SOURCES     = fop.c fop.h fop_format.c fop_format.h \
                                fop_user.h fop_format_def.h fop_format_c.c

INCLUDES                      = -I. -I$(top_srcdir) -I$(top_srcdir)/include
EXTRA_DIST                    = fop2c.in
