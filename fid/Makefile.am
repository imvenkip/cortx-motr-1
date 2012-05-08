fiddir                     = $(includedir)/colibri/fid
fid_HEADERS                = fid.h

fid_u.h fid_u.c: fid.ff \
                  $(top_builddir)/fop/rt/libc2rt.la \
                  $(top_builddir)/fop/fop2c 
	$(top_builddir)/fop/fop2c -ku $<

noinst_LTLIBRARIES         = libcolibri-fid.la
libcolibri_fid_la_SOURCES  = fid_u.c fid_u.h fid.h fid.c
INCLUDES                   = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

EXTRA_DIST                 = fid.ff

clean-local:
	cd @SRCDIR@/fid ; \
	rm -fr fid_u.[ch]
