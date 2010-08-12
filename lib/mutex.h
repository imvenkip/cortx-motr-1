/* -*- C -*- */

#ifndef __COLIBRI_LIB_MUTEX_H__
#define __COLIBRI_LIB_MUTEX_H__

#include "lib/types.h"

/**
   @defgroup mutex Mutual exclusion synchronisation object
   @{
*/

#ifndef __KERNEL__
#include "lib/user_space/mutex.h"
#else
#include "lib/linux_kernel/mutex.h"
#endif

/* struct c2_mutex is defined by headers above. */

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
   Try to take a mutex lock.
   Returns 0 with the mutex locked,
   or non-zero if lock is already hold by others.
 */
int c2_mutex_trylock(struct c2_mutex *mutex);


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
