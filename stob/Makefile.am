stobdir                     = $(includedir)/stob
stob_HEADERS                = ad.h linux_getevents.h linux.h \
                              linux_internal.h stob.h

if LINUX
STOB_SOURCES                = stob.c stob.h linux.c linux.h \
                              linux_adieu.c linux_internal.h \
                              linux_getevents.h ad.c ad.h \
			      stob_id.c stob_id.h
else
STOB_SOURCES                = stob.c stob.h stob_id.c stob_id.h
endif

noinst_LTLIBRARIES          = libcolibri-stob.la
libcolibri_stob_la_SOURCES  = $(STOB_SOURCES)
EXTRA_DIST                  = stob.c stob.h stob_id.c stob_id.h linux.c linux.h linux_adieu.c linux_internal.h linux_getevents.h
