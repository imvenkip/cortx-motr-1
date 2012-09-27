layoutdir                     = $(includedir)/colibri/layout
layout_HEADERS                = layout.h layout_internal.h pdclust.h \
                                composite.h list_enum.h linear_enum.h \
                                layout_db.h

noinst_LTLIBRARIES            = libcolibri-layout.la
libcolibri_layout_la_SOURCES  = layout.c layout.h layout_internal.h \
                                pdclust.c pdclust.h composite.c composite.h \
                                list_enum.c list_enum.h linear_enum.c \
                                linear_enum.h layout_db.c layout_db.h

INCLUDES                      = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include
