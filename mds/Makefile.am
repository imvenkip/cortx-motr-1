sbin_PROGRAMS = mds

mds_SOURCES   = mds.c
mds_LDFLAGS   = @LDFLAGS@
mds_LDADD     = $(top_builddir)/lib/libc2.la \
                $(top_builddir)/fol/libcolibri-fol.la \
                @DB4LIBS@

# Make sure that dtm headers will be found
INCLUDES      = -I. -I$(top_srcdir)/dtm -I$(top_srcdir)/include
