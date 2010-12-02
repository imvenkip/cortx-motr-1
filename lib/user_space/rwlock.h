/* -*- C -*- */

#ifndef __COLIBRI_LIB_USER_SPACE_RWLOCK_H__
#define __COLIBRI_LIB_USER_SPACE_RWLOCK_H__


/**
   @addtogroup rwlock

   <b>User space rwlock.</b>
   @{
*/

#include <pthread.h>
/**
   Blocking read-write lock.
 */
struct c2_rwlock {
        pthread_rwlock_t rw_lock;
};

/** @} end of rwlock group */

/* __COLIBRI_LIB_USER_SPACE_RWLOCK_H__ */
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
