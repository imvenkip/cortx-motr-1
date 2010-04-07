SUBDIRS    = . ut

noinst_LTLIBRARIES   = libc2.la
libc2_la_SOURCES     = libc2.c memory.c c2list.c c2bitops.c
libc2_la_LDFLAGS     = -static
libc2_la_CFLAGS      = -fPIC
libc2_la_LIBADD      = @LIBS@

INCLUDES             = -I. -I$(top_srcdir)/include
EXTRA_DIST           = adt.h cc.h memory.h c2list.h c2bitops.h c2bitmap.h
