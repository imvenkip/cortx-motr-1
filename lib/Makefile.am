SUBDIRS    = . ut

noinst_LTLIBRARIES   = libc2.la
libc2_la_SOURCES     = libc2.c memory.c c2list.c c2bitops.c refs.c cc.c c2queue.c cache.c
#libc2_la_LDFLAGS     = -release @LT_RELEASE@
#libc2_la_LDFLAGS     = -static
libc2_la_CFLAGS      = -fPIC
libc2_la_LIBADD      = @LIBS@ @DB4LIBS@ @PTHREAD_LIBS@

INCLUDES             = -I. -I$(top_srcdir)/include -I $(DB4SRC)/build_unix
EXTRA_DIST           = adt.h cc.h memory.h c2list.h c2bitops.h c2bitmap.h refs.h atomic.h c2queue.h cache.h
