lib_LTLIBRARIES            = libcolibri-sns.la
libcolibri_sns_la_SOURCES  = sns.c sns.h repair.c repair.h
libcolibri_sns_la_LDFLAGS  = -release @LT_RELEASE@
libcolibri_sns_la_LIBADD   = $(top_builddir)/lib/libc2.la @LIBS@

INCLUDES                   = -I. -I$(top_srcdir)/include -I$(top_srcdir)
EXTRA_DIST                 = poolmach.c
