fopdir                        = $(includedir)/fop
noinst_LTLIBRARIES            = libc2rt.la

fop_SOURCES                   = ../fop_base.c ../fop_base.h ../fom.h \
                                ../fop_format.c \
                                ../fop_format.h \
                                ../fop_user.h ../fop_format_def.h \
                                ../fop_format_c.c \
                                stub.c

libc2rt_la_SOURCES            = $(fop_SOURCES)
libc2rt_la_LIBADD             = $(top_builddir)/addb/addb_pseudo/libcolibri-addb-pseudo.la \
                                $(top_builddir)/fol/libcolibri-fol.la \
                                $(top_builddir)/db/libcolibri-db.la \
                                $(top_builddir)/lib/libc2.la

INCLUDES                      = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include
