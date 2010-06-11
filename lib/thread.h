/* -*- C -*- */

#ifndef __COLIBRI_LIB_THREAD_H__
#define __COLIBRI_LIB_THREAD_H__

#include <sys/types.h>
#include <pthread.h>
#include <signal.h>

#include "cdefs.h"
#include "chan.h"

/**
   @defgroup thread Thread

   Thread creation and manipulation interface (rather spartan at the moment).

   Threads can be created that will start execution by running a specified
   function invoked with a specified argument. Thread functions return void, a
   user must arrange a method of transferring results back from the thread if
   necessary.

   The only operation on a started thread is waiting for its completion
   ("joining" the thread).

   @see c2_thread

   @{
 */

/**
   Thread states used to validate thread interface usage.

   @see c2_thread
 */
enum c2_thread_state {
	TS_PARKED = 0,
	TS_RUNNING
};

/**
   Control block for a thread.

   <b>States</b>

   A thread control block can be in one of the following states:

   @li PARKED: thread is not yet started or has been joined already. In this
   state c2_thread::t_func is NULL;

   @li RUNNING: the thread started execution of c2_thread::t_func function, but
   hasn't yet been joined. Note that the thread can be in this state after
   return from c2_thread::t_func.

   <b>Concurrency control</b>

   A user is responsible for serialising access to a control block. For example,
   there should be no concurrent calls to c2_thread_init() or c2_thread_join()
   for the same c2_thread.

   <b>Liveness</b>

   Implementation only accesses control block as part of explicit calls to
   c2_thread interface functions. A user is free to destroy the control block at
   any moment, except for the possible resource leak in the case of running (and
   not yet joined) thread.
 */
struct c2_thread {
	enum c2_thread_state t_state;
	/** POSIX thread identifier for now. */
	pthread_t            t_id;
	int                (*t_init)(void *);
	void               (*t_func)(void *);
	void                *t_arg;
	struct c2_chan       t_initwait;
	int                  t_initrc;
};

/**
   Type-safe wrapper around c2_thread_init().

   With this macro one can initialise a thread with a function taking an
   argument of a particular type:

   @code
   static void worker(struct foo *arg) { ... }
   static struct c2_thread tcb;

   result = C2_THREAD_INIT(&tcb, struct foo, NULL, worker, arg);
   @endcode

   C2_THREAD_INIT() checks that type of the argument matches function prototype.

   @note TYPE cannot be void.
 */
#define C2_THREAD_INIT(thread, TYPE, init, func, arg)	\
({							\
	typeof(func) __func = (func);			\
	typeof(arg)  __arg  = (arg);			\
	TYPE         __dummy;				\
	(void)(__func == (void (*)(TYPE))NULL);		\
	(void)(&__arg == &__dummy);			\
	c2_thread_init(thread,				\
                       (int  (*)(void *))init,  	\
		       (void (*)(void *))__func,	\
		       (void *)(unsigned long)__arg);	\
})

/**
   Helper macro creating an anonymous function with a given body.

   For example:

   @code
   int x;

   result = C2_THREAD_INIT(&tcb, int, NULL, 
                           LAMBDA(void, (int y) { printf("%i", x + y); } ));
   @endcode

   LAMBDA is useful to create an "ad-hoc" function that can be passed as a
   call-back function pointer.

   @note resulting anonymous function can be called only while the block where
   LAMBDA macro was called is active. For example, in the code fragment above,
   the tcb thread must finish before the block where C2_THREAD_INIT() was called
   is left.

   @see http://en.wikipedia.org/wiki/Lambda_calculus
 */
#define LAMBDA(T, ...) ({ T __lambda __VA_ARGS__; &__lambda; })

/**
   Creates a new thread.

   If "init" is not NULL, the created thread starts execution by calling
   (*init)(arg). If this call returns non-zero, thread exits and
   c2_thread_init() returns the value returned by "init".

   Otherwise (or in the case where "init" is NULL), c2_thread_init() returns 0
   and the thread calls (*func)(arg) and exits when this call completes.

   @note it is possible that after successful return from c2_thread_init() the
   thread hasn't yet entered "func" code, it is also possible that the thread
   has finished its execution.

   @pre q->t_state == TS_PARKED
   @post (result != 0) == (q->t_state == TS_PARKED)
   @post (result == 0) == (q->t_state == TS_RUNNING)
 */
int  c2_thread_init(struct c2_thread *q, int (*init)(void *),
		    void (*func)(void *), void *arg);

/**
   Releases resources associated with the thread.

   @pre q->t_state == TS_PARKED
 */
void c2_thread_fini(struct c2_thread *q);

/**
   Waits until the thread exits.

   After this calls returns successfully it is guaranteed that no code would be
   ever executed by the "q", including instructions that touch stack or code
   pages. Note that the same effect can not be reliably achieved by the explicit
   synchronization (e.g., by signalling a condition variable at the end of a
   thread function), because the thread might be still executing instructions
   after it returns from c2_thread::t_func.

   @pre q->t_state == TS_RUNNING
   @pre q is different from the calling thread
   @post (result == 0) == (q->t_state == TS_PARKED)
 */
int  c2_thread_join(struct c2_thread *q);

int  c2_threads_init(void);
void c2_threads_fini(void);


/** @} end of thread group */

/* __COLIBRI_LIB_THREAD_H__ */
#endif

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
