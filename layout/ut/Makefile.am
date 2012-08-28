INCLUDES         = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

noinst_PROGRAMS  = ldemo

ldemo_SOURCES   = ldemo.c
ldemo_LDFLAGS   = -pthread -lm
ldemo_LDADD     = $(top_builddir)/colibri/libcolibri.la

EXTRA_DIST      = ldemo_internal.c

noinst_LTLIBRARIES = liblayout-ut.la
liblayout_ut_la_SOURCES = layout.c

