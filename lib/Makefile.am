SUBDIRS              = user_space

libc2dir             = $(includedir)/lib
libc2_HEADERS        = adt.h atomic.h cond.h getopts.h misc.h refs.h trace.h \
                       ut.h arith.h cdefs.h errno.h list.h mutex.h rwlock.h \
                       types.h vec.h assert.h chan.h ext.h memory.h queue.h \
                       thread.h time.h ub.h bitmap.h processor.h

noinst_LTLIBRARIES   = libc2.la
libc2_la_SOURCES     = libc2.c memory.c list.c refs.c rwlock.c \
                       queue.c vec.c chan.c mutex.c cond.c \
                       assert.c time.c \
                       adt.h rwlock.h memory.h list.h \
                       refs.h atomic.h queue.h arith.h thread.h \
                       vec.h chan.h mutex.h cond.h types.h errno.h \
                       user_space/user_x86_64_atomic.h assert.h cdefs.h \
                       misc.c misc.h ext.c ext.h bitstring.h bitstring.c \
                       ut.c ut.h ub.c ub.h trace.h user_space/trace.c \
                       getopts.c getopts.h user_space/assert.h \
                       user_space/mutex.h user_space/cdefs.h \
                       user_space/utime.c user_space/types.h \
                       user_space/thread.c user_space/thread.h \
		       user_space/processor.c

libc2_la_LDFLAGS       = @PTHREAD_LIBS@
EXTRA_DIST             = linux_kernel/assert.h linux_kernel/cdefs.h \
                         linux_kernel/memory.c linux_kernel/mutex.c \
                         linux_kernel/mutex.h linux_kernel/ktime.c \
                         linux_kernel/types.h linux_kernel/processor.c
