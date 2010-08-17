if LINUX
STOB_SOURCES                = stob.c stob.h linux.c linux.h \
                              linux_adieu.c linux_internal.h \
                              linux_getevents.h adata_db.c
else
STOB_SOURCES                = stob.c stob.h
endif

noinst_LTLIBRARIES          = libcolibri-stob.la
libcolibri_stob_la_SOURCES  = $(STOB_SOURCES)
INCLUDES                    = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I@DBSRC@ -I@DBSRC@/build_unix
EXTRA_DIST                  = stob.c stob.h linux.c linux.h linux_adieu.c linux_internal.h linux_getevents.h
