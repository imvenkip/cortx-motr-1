fopdir                        = $(includedir)/fop
noinst_LTLIBRARIES            = libc2rt.la libcolibri-fop.la

fop_HEADERS                   = fop.h fom.h fop_format.h fop_user.h fop_format_def.h

fop_SOURCES                   = fop.c fop.h fom.h fop_format.c fop_format.h \
                                fop_user.h fop_format_def.h fop_format_c.c \
                                fop_iterator.h fop_iterator.c

libcolibri_fop_la_SOURCES     = $(fop_SOURCES)

libc2rt_la_SOURCES            = $(fop_SOURCES)
libc2rt_la_LIBADD             = $(top_builddir)/addb/libcolibri-addb.la \
                                $(top_builddir)/fol/libcolibri-fol.la \
                                $(top_builddir)/db/libcolibri-db.la \
                                $(top_builddir)/fid/libcolibri-fid.la \
                                $(top_builddir)/lib/libc2.la

INCLUDES                      = -I. -I$(top_srcdir) -I$(top_srcdir)/include
EXTRA_DIST                    = fop2c.in
