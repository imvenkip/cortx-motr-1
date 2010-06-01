sbin_PROGRAMS = mds

mds_SOURCES   = mds.c
mds_LDADD     = $(top_builddir)/lib/libc2.la \
                $(top_builddir)/fol/libcolibri-fol.la \
                @DBLIBS@

# Make sure that dtm headers will be found
INCLUDES      = -I. -I$(top_srcdir) -I$(top_srcdir)/include
