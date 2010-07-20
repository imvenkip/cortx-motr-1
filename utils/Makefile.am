SUBDIRS    = ploss

noinst_PROGRAMS = ut ub

ut_SOURCES   = ut_main.c 
ut_LDFLAGS   = -pthread
ut_LDADD     = $(top_builddir)/colibri/libcolibri.la \
               $(top_builddir)/lib/ut/libc2-ut.la    \
               $(top_builddir)/stob/ut/libstob-ut.la \
               $(top_builddir)/fop/ut/libfop-ut.la

ub_SOURCES   = ub_main.c 
ub_LDFLAGS   = -pthread
ub_LDADD     = $(top_builddir)/colibri/libcolibri.la \
               $(top_builddir)/lib/ut/libc2-ut.la    \
               $(top_builddir)/stob/ut/libstob-ut.la \
               $(top_builddir)/fop/ut/libfop-ut.la
