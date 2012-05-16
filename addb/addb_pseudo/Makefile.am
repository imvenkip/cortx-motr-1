# in order to build addb library, fop/fop2c is needed. But fop/fop2c
# needs support from addb library. So we add a pseudo addb library,
# which is only a pseudo framework of addb.
# after fop is built, we build addb library again with full functionally.

noinst_LTLIBRARIES          = libcolibri-addb-pseudo.la
libcolibri_addb_pseudo_la_SOURCES  = ../addb.c ../addb_console.c addb_pseudo.c

INCLUDES                    = -iquote $(top_srcdir)/include
