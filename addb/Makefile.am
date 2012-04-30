# in order to build addb library, fop/fop2c is needed. But fop/fop2c
# needs support from addb library. So we add a pseudo addb library,
# which is only a pseudo framework of addb.
# after fop is built, we build addb library again with full functionally.

addbdir                     = $(includedir)/colibri/addb
addb_HEADERS                = addb.h addb_u.h addb_k.h

noinst_LTLIBRARIES          = libcolibri-addb.la
libcolibri_addb_la_SOURCES  = addb_u.h addb_u.c addb.c addb.h addb_console.c \
			      addb_fop.c addb_store.c

INCLUDES                    = -iquote . -iquote $(top_srcdir)/include

addb_u.h addb_u.c: addb.ff \
                  $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -uk $<

addb.c : addb.h addb_u.h

EXTRA_DIST       = addb.ff linux_kernel/addb_console.c

clean-local:
	cd @SRCDIR@/addb ; \
	rm -fr addb_[uk].[hc]
