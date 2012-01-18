# in order to build addb library, fop/fop2c is needed. But fop/fop2c
# needs support from addb library. So we add a pseudo addb library,
# which is only a pseudo framework of addb.
# after fop is built, we build addb library again with full functionally.

addbdir                     = $(includedir)/addb
addb_HEADERS                = addb.h

noinst_LTLIBRARIES          = libcolibri-addb.la
libcolibri_addb_la_SOURCES  = addb_u.h addb_u.c addb.c addb.h addb_console.c \
			      addb_fop.c addb_store.c

INCLUDES                    = -I. -I$(top_srcdir)/include

addb_u.h addb_u.c: addb.ff \
                  $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -uk $<

addb.c : addb.h addb_u.h

EXTRA_DIST       = addb.ff

clean-local:
	rm -fr addb_[uk].[hc]
