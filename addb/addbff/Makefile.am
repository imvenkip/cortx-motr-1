# fop generated files are required by other directories

addbdir                      = $(includedir)/colibri/addb

ADDB_FOPS                    = addb.h addb.c

noinst_LTLIBRARIES           = libcolibri-addbff.la
libcolibri_addbff_la_SOURCES = $(ADDB_FOPS)

INCLUDES                     = -iquote . -iquote .. \
			       -iquote $(top_srcdir)/include

EXTRA_DIST                   = addb.ff

$(ADDB_FOPS): addb.ff
	$(top_builddir)/xcode/ff2c/ff2c $<

MOSTLYCLEANFILES             = $(ADDB_FOPS)
