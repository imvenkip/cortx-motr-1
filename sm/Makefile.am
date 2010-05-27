lib_LTLIBRARIES           = libcolibri-sm.la
libcolibri_sm_la_SOURCES  = sm.c
libcolibri_sm_la_LDFLAGS  = -release @LT_RELEASE@
libcolibri_sm_la_LIBADD   = $(top_builddir)/lib/libc2.la @LIBS@

EXTRA_DIST                = sm.h
INCLUDES                  = -I. -I$(top_srcdir) -I$(top_srcdir)/include
