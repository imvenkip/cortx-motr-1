if LINUX
STOB_SOURCES                = stob.c stob.h linux.c linux.h \
                              linux_adieu.c linux_internal.h \
                              linux_getevents.h ad.c ad.h
else
STOB_SOURCES                = stob.c stob.h
endif

noinst_LTLIBRARIES          = libcolibri-stob.la
libcolibri_stob_la_SOURCES  = $(STOB_SOURCES)
EXTRA_DIST                  = stob.c stob.h linux.c linux.h linux_adieu.c linux_internal.h linux_getevents.h
