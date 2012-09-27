# fop generated files are required by other directories

include $(top_srcdir)/Makefile.ff

addbdir                      = $(includedir)/colibri/addb

ADDB_FOPS                    = addb_ff.h addb_ff.c

noinst_LTLIBRARIES           = libcolibri-addbff.la
libcolibri_addbff_la_SOURCES = $(ADDB_FOPS)

INCLUDES                     = -iquote . -iquote .. \
			       -iquote $(top_srcdir)/include

EXTRA_DIST                   = addb.ff

MOSTLYCLEANFILES             = $(ADDB_FOPS)
