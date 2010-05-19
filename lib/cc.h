/* -*- C -*- */

#ifndef __COLIBRI_CC_H__
#define __COLIBRI_CC_H__

#include "lib/cdefs.h"

/**
   @defgroup cc Concurrency control
   @{
 */

/**
   Blocking read-write lock.
 */
struct c2_rwlock {
};

/**
   read-write lock constructor
 */
void c2_rwlock_init(struct c2_rwlock *lock);

/**
   read-write lock destructor
 */
void c2_rwlock_fini(struct c2_rwlock *lock);

/**
   take exclusive lock
 */
void c2_rwlock_write_lock(struct c2_rwlock *lock);
/**
   release exclusive lock
 */
void c2_rwlock_write_unlock(struct c2_rwlock *lock);

/**
   take shared lock
 */
void c2_rwlock_read_lock(struct c2_rwlock *lock);
/**
   release shared lock
 */
void c2_rwlock_read_unlock(struct c2_rwlock *lock);

struct c2_semaphore {
};

/** @} end of cc group */


/* __COLIBRI_CC_H__ */
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
