noinst_LTLIBRARIES   = libc2.la
libc2_la_SOURCES     = libc2.c memory.c list.c refs.c rwlock.c \
                       queue.c vec.c thread.c chan.c mutex.c cond.c \
                       assert.c adt.h rwlock.h memory.h list.h \
                       refs.h atomic.h queue.h arith.h thread.h \
                       vec.h chan.h mutex.h cache.h cond.h \
	               user_space/user_x86_64_atomic.h assert.h cdefs.h \
                       misc.c misc.h ext.c ext.h \
                       ut.c ut.h ub.c ub.h trace.h user_space/trace.c
INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include \
                       -I@DBSRC@/build_unix 

