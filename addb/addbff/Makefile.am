# fop generated files are required by other directories

addbdir                     = $(includedir)/colibri/addb

noinst_LTLIBRARIES          = libcolibri-addbff.la
libcolibri_addbff_la_SOURCES = addb_u.h addb_u.c

INCLUDES                    = -iquote . -iquote .. -iquote $(top_srcdir)/include

EXTRA_DIST                  = addb.ff

addb_u.h addb_u.c: addb.ff \
                   $(top_builddir)/fop/rt/libc2rt.la $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -uk $<

clean-local:
	rm -fr addb_[uk].[hc]
