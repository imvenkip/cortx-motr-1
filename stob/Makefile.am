stobdir                     = $(includedir)/colibri/stob
stob_HEADERS                = ad.h linux_getevents.h linux.h \
                              linux_internal.h stob.h

io_fop_u.h io_fop_u.c : io_fop.ff \
                        $(top_builddir)/fop/rt/libc2rt.la \
                        $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -u $<

EXTRA_DIST = io_fop.ff

libcolibri_stob_la_SOURCES  = stob.c stob.h linux.c linux.h \
                              linux_adieu.c linux_internal.h \
                              linux_getevents.h ad.c ad.h \
			      io_fop_u.h io_fop_u.c io_fop.c \
			      io_fop.h stob_id.c stob_id.h

noinst_LTLIBRARIES          = libcolibri-stob.la
