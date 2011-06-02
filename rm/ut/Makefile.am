noinst_LTLIBRARIES   = librm-ut.la
librm_ut_la_SOURCES  = rings.h rings.c rmut.c
INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include

noinst_PROGRAMS  = rmls

rmls_SOURCES   = rmls.c
rmls_LDFLAGS   =
rmls_LDADD     = $(top_builddir)/colibri/libcolibri.la

