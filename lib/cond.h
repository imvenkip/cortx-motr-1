/* -*- C -*- */

#ifndef __COLIBRI_LIB_COND_H__
#define __COLIBRI_LIB_COND_H__

#include "chan.h"

struct c2_mutex;

/**
   @defgroup cond Conditional variable.

   Condition variable is a widely known and convenient synchronization
   mechanism.

   Notionally, a condition variable packages two things: a predicate ("a
   condition", hence the name) on computation state, e.g., "a free buffer is
   available", "an incoming request waits for processing" or "all worker threads
   have finished", and a mutex (c2_mutex) protecting changes to the state
   affecting the predicate.

   There are two parts in using condition variable:

   @li in all places where state is changed in a way that affects the predicate,
   the condition variable associated with the predicate has to be signalled. For
   example:

   @code
   buffer_put(struct c2_buffer *buf) {
           // buffer is being returned to the pool...
           c2_mutex_lock(&buffer_pool_lock);
           // add the buffer to the free list
           c2_list_add(&buffer_pool_free, &buf->b_linkage);
           // and signal the condition variable
           c2_cond_signal(&buffer_pool_hasfree, &buffer_pool_lock);
           c2_mutex_unlock(&buffer_pool_lock);
   }
   @endcode

   @li to wait for predicate to change, one takes the lock, checks the predicate
   and calls c2_cond_wait() until the predicate becomes true:

   @code
   struct c2_buffer *buffer_put(void) {
           struct c2_buffer *buf;

           c2_mutex_lock(&buffer_pool_lock);
           while (c2_list_is_empty(&buffer_pool_free))
                   c2_cond_wait(&buffer_pool_hasfree, &buffer_pool_lock);
           buf = c2_list_first(&buffer_pool_free);
           c2_list_del(&buf->b_linkage);
           c2_mutex_unlock(&buffer_pool_lock);
           return buf;
   }
   @endcode

   Note that one has to re-check the predicate after c2_cond_wait() returns,
   because it might, generally, be false if multiple threads are waiting for
   predicate change (if, in our case, there are multiple concurrent calls to
   buffer_get()). This introduces one of the nicer features of condition
   variables: de-coupling of producers and consumers.

   Condition variables are more reliable and structured synchronization
   primitive than channels (c2_chan), because the lock, protecting the predicate
   is part of the interface and locking state can be checked. On the other hand,
   channels can be used with predicates protected by read-write locks, atomic
   variables, etc.---where condition variables are not applicable.

   @see c2_chan
   @see http://opengroup.org/onlinepubs/007908799/xsh/pthread_cond_wait.html

   @todo Consider supporting other types of locks in addition to c2_mutex.

   @{
*/

struct c2_cond {
	struct c2_chan c_chan;
};

void c2_cond_init(struct c2_cond *cond);
void c2_cond_fini(struct c2_cond *cond);

/**
   Atomically unlocks the mutex, waits on the condition variable and locks the
   mutex again before returning.

   @pre  c2_mutex_is_locked(mutex)
   @post c2_mutex_is_locked(mutex)
 */
void c2_cond_wait(struct c2_cond *cond, struct c2_mutex *mutex);

/**
   Wakes up no more than one thread waiting on the condition variable.

   @pre c2_mutex_is_locked(mutex)
 */
void c2_cond_signal(struct c2_cond *cond, struct c2_mutex *mutex);

/**
   Wakes up all threads waiting on the condition variable.

   @pre c2_mutex_is_locked(mutex)
 */
void c2_cond_broadcast(struct c2_cond *cond, struct c2_mutex *mutex);

/** @} end of cond group */

/* __COLIBRI_LIB_COND_H__ */
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
