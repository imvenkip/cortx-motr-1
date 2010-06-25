if LINUX
STOB_SOURCES                = stob.c stob.h linux.c linux.h linux_adieu.c \
                              linux_internal.h
else
STOB_SOURCES                = stob.c stob.h
endif

lib_LTLIBRARIES             = libcolibri-stob.la
libcolibri_stob_la_SOURCES  = $(STOB_SOURCES)
libcolibri_stob_la_LDFLAGS  = -release @LT_RELEASE@
libcolibri_stob_la_LIBADD   = $(top_builddir)/lib/libc2.la

INCLUDES                    = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I@DBSRC@/build_unix

# Make sure that dist contains all sources even not on Linux
EXTRA_DIST                  = main.c stob.c stob.h linux.c linux.h linux_adieu.c linux_internal.h