sbin_PROGRAMS = mds

mds_SOURCES   = mds.c
mds_LDFLAGS   = @LDFLAGS@
mds_CFLAGS    = -DENABLE_USER_MDS
mds_LDADD     = @DB4LIBS@

INCLUDES      = -I. -I$(top_srcdir)/include
SUBDIRS       = kernel
