/* -*- C -*- */

#ifndef __COLIBRI_LIB_MUTEX_H__
#define __COLIBRI_LIB_MUTEX_H__

#include <pthread.h>

#include "cdefs.h"

/**
   @defgroup mutex Mutual exclusion synchronisation object
   @{
*/

struct c2_mutex {
	/* POSIX mutex for now. */
	pthread_mutex_t m_impl;
	pthread_t       m_owner;
};

void c2_mutex_init(struct c2_mutex *mutex);
void c2_mutex_fini(struct c2_mutex *mutex);

/**
   Returns with the mutex locked.

   @pre  c2_mutex_is_not_locked(mutex)
   @post c2_mutex_is_locked(mutex)
 */
void c2_mutex_lock(struct c2_mutex *mutex);

/**
   Unlocks the mutex.

   @pre  c2_mutex_is_locked(mutex)
   @post c2_mutex_is_not_locked(mutex)
 */
void c2_mutex_unlock(struct c2_mutex *mutex);

/**
   True iff mutex is locked by the calling thread.

   @note this function can be used only in assertions.
 */
bool c2_mutex_is_locked(const struct c2_mutex *mutex);

/**
   True iff mutex is not locked by the calling thread.

   @note this function can be used only in assertions.

   @note that this function is *not* necessary equivalent to
   !c2_mutex_is_locked(mutex).
 */
bool c2_mutex_is_not_locked(const struct c2_mutex *mutex);


/** @} end of mutex group */


/* __COLIBRI_LIB_MUTEX_H__ */
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
