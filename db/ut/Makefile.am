noinst_LTLIBRARIES   = libdb-ut.la
libdb_ut_la_SOURCES  = db.c emap.c
INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include

noinst_PROGRAMS  = elist

elist_SOURCES   = elist.c
elist_LDFLAGS   =
elist_LDADD     = $(top_builddir)/colibri/libcolibri.la

