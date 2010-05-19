SUBDIRS    = . ut

lib_LTLIBRARIES      = libc2.la
libc2_la_SOURCES     = libc2.c memory.c c2list.c c2bitops.c refs.c cc.c \
                       queue.c vec.c thread.c chan.c mutex.c cache.c \
                       cond.c
libc2_la_LDFLAGS     = -release @LT_RELEASE@ 
#libc2_la_CFLAGS      = -fPIC
libc2_la_LIBADD      = @LIBS@ @DB4LIBS@ @PTHREAD_LIBS@

INCLUDES             = -I. -I$(top_srcdir)/include -I@DB4SRC@/build_unix 
EXTRA_DIST           = adt.h cc.h memory.h c2list.h c2bitops.h c2bitmap.h \
                       refs.h atomic.h queue.h arith.h thread.h vec.h \
                       chan.h mutex.h cache.h cond.h
