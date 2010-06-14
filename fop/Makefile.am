SUBDIRS                    = . ut

lib_LTLIBRARIES            = libcolibri-fop.la
libcolibri_fop_la_SOURCES  = fop.c fop.h fop_format.c
libcolibri_fop_la_LDFLAGS  = -release @LT_RELEASE@
libcolibri_fop_la_LIBADD   = $(top_builddir)/lib/libc2.la

INCLUDES                   = -I. -I$(top_srcdir)/include -I$(top_srcdir)
