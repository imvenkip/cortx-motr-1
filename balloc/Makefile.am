INCLUDES         = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I@DBSRC@/build_unix

noinst_PROGRAMS  = balloc

balloc_SOURCES   = balloc.c balloc.h
balloc_LDADD     = @DBLIBS@ $(top_builddir)/colibri/libcolibri.la
