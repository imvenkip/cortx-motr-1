sbin_PROGRAMS = mds

mds_SOURCES   = mds.c
mds_LDFLAGS   = @LDFLAGS@
mds_LDADD     = $(top_builddir)/lib/libc2.la \
                $(top_builddir)/fol/libfol.la \
                @DB4LIBS@

INCLUDES      = -I. -I$(top_srcdir)/include
