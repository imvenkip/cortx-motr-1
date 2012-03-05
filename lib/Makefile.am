SUBDIRS              = user_space

libc2dir             = $(includedir)/colibri/lib
libc2_HEADERS        = \
                       adt.h \
                       arith.h \
                       assert.h \
                       atomic.h \
                       bitmap.h \
                       bob.h \
                       cdefs.h \
                       chan.h \
                       cond.h \
                       errno.h \
                       ext.h \
                       getopts.h \
                       list.h \
                       memory.h \
                       misc.h \
                       mutex.h \
                       processor.h \
                       queue.h \
                       refs.h \
                       rwlock.h \
                       semaphore.h \
                       thread.h \
                       time.h \
                       timer.h \
                       tlist.h \
                       trace.h \
                       types.h \
                       ub.h \
                       ut.h \
                       vec.h

noinst_LTLIBRARIES   = libc2.la
libc2_la_SOURCES     = \
                       adt.h \
                       arith.h \
                       assert.c \
                       assert.h \
                       atomic.h \
                       bitmap.c \
                       bitstring.c \
                       bitstring.h \
                       bob.c \
                       bob.h \
                       buf.c \
                       buf.h \
                       c2lib.c \
                       cdefs.h \
                       chan.c \
                       chan.h \
                       cond.c \
                       cond.h \
                       errno.h \
                       ext.c \
                       ext.h \
                       getopts.c \
                       getopts.h \
                       list.c \
                       list.h \
                       memory.h \
                       misc.c \
                       misc.h \
                       mutex.c \
                       mutex.h \
                       queue.c \
                       queue.h \
                       refs.c \
                       refs.h \
                       rwlock.h \
                       semaphore.h \
                       thread.c \
                       thread.h \
                       time.c \
                       timer.c \
                       tlist.c \
                       tlist.h \
                       trace.c \
                       trace.h \
                       types.h \
                       ub.c \
                       ub.h \
                       user_space/assert.h \
                       user_space/cdefs.h \
                       user_space/memory.c \
                       user_space/mutex.h \
                       user_space/processor.c \
                       user_space/rwlock.c \
                       user_space/semaphore.c \
                       user_space/semaphore.h \
                       user_space/thread.h \
                       user_space/timer.h \
                       user_space/utrace.c \
                       user_space/trace.h \
                       user_space/types.h \
                       user_space/user_x86_64_atomic.h \
                       user_space/uthread.c \
                       user_space/utime.c \
                       ut.c \
                       ut.h \
                       vec.c \
                       vec.h

libc2_la_LDFLAGS       = @PTHREAD_LIBS@ -lrt
EXTRA_DIST             = \
                         linux_kernel/assert.h \
                         linux_kernel/atomic64.h \
                         linux_kernel/cdefs.h \
                         linux_kernel/kthread.c \
                         linux_kernel/ktrace.c \
                         linux_kernel/ktime.c \
                         linux_kernel/memory.c \
                         linux_kernel/mutex.c \
                         linux_kernel/mutex.h \
                         linux_kernel/processor.c \
                         linux_kernel/semaphore.c \
                         linux_kernel/semaphore.h \
                         linux_kernel/thread.h \
                         linux_kernel/timer.c \
                         linux_kernel/time.h \
                         linux_kernel/timer.h \
                         linux_kernel/vec.h \
                         linux_kernel/kvec.c \
                         linux_kernel/rwlock.c \
                         linux_kernel/rwlock.h \
                         linux_kernel/ut.c \
                         linux_kernel/main.c \
                         linux_kernel/types.h
