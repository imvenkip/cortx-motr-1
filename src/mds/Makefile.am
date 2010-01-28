sbin_PROGRAMS = mds

mds_SOURCES   = mds.c
mds_LDFLAGS   = @LDFLAGS@
mds_CFLAGS    = -DENABLE_USER_MDS @CFLAGS@
mds_LDADD     = @DB4LIBS@

INCLUDES      = -I$(top_srcdir)/include
SUBDIRS       = kernel
