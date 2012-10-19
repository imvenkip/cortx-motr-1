include $(top_srcdir)/Makefile.ff

fiddir                     = $(includedir)/colibri/fid
fid_HEADERS                = fid.h fid_ff.h

FID_FOPS_SOURCES           = fid_ff.h fid_ff.c

noinst_LTLIBRARIES         = libcolibri-fid.la
libcolibri_fid_la_SOURCES  = $(FID_FOPS_SOURCES) fid.c fid.h
INCLUDES                   = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

EXTRA_DIST                 = fid.ff

MOSTLYCLEANFILES           = $(FID_FOPS_SOURCES)
