# in order to build addb library, fop/fop2c is needed. But fop/fop2c
# needs support from addb library. So we add a pseudo addb library,
# which is only a pseudo framework of addb.
# after fop is built, we build addb library again with full functionally.

addbdir                     = $(includedir)/colibri/addb
addb_HEADERS                = addb.h

noinst_LTLIBRARIES          = libcolibri-addb.la
libcolibri_addb_la_SOURCES  = addb.c addb.h addb_console.c \
			      addb_fop.c addb_store.c

INCLUDES                    = -iquote . -iquote $(top_srcdir)/include

EXTRA_DIST                  = linux_kernel/addb_console.c
