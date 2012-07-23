# fop generated files are required by other directories

addbdir                     = $(includedir)/colibri/addb

noinst_LTLIBRARIES          = libcolibri-addbff.la
libcolibri_addbff_la_SOURCES = addb.h addb.c

INCLUDES                    = -iquote . -iquote .. -iquote $(top_srcdir)/include

EXTRA_DIST                  = addb.ff

addb.h addb.c: addb.ff
	$(top_builddir)/xcode/ff2c/ff2c $<

clean-local:
	rm -fr addb.[hc]
