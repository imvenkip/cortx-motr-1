noinst_PROGRAMS   = ut

ut_SOURCES   = main.c 
ut_LDFLAGS   = -pthread
ut_LDADD     = $(top_builddir)/colibri/libcolibri.la \
               $(top_builddir)/lib/ut/libc2-ut.la    \
               $(top_builddir)/stob/ut/libstob-ut.la \
               $(top_builddir)/fop/ut/libfop-ut.la

INCLUDES     = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I @DBSRC@/build_unix
