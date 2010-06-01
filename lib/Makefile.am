SUBDIRS              = . ut

lib_LTLIBRARIES      = libc2.la
libc2_la_SOURCES     = libc2.c memory.c c2list.c c2bitops.c refs.c rwlock.c \
                       queue.c vec.c thread.c chan.c mutex.c cache.c cond.c \
                       assert.c adt.h rwlock.h memory.h c2list.h c2bitops.h \
                       c2bitmap.h refs.h atomic.h queue.h arith.h thread.h \
                       vec.h chan.h mutex.h cache.h cond.h user_x86_64_atomic.h \
                       assert.h cdefs.h
libc2_la_LDFLAGS     = -release @LT_RELEASE@ 
libc2_la_LIBADD      = @DBLIBS@ @PTHREAD_LIBS@

INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I@DBSRC@/build_unix 

