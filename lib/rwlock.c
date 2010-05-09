/* -*- C -*- */

#include "asrt.h"
#include "rwlock.h"

/**
   @defgroup rwlock Read-write lock
   @{
 */

void c2_rwlock_init(struct c2_rwlock *lock)
{
	int rc;

	rc = pthread_rwlock_init(&lock->rw_lock, NULL);
	C2_ASSERT(rc == 0);
}

void c2_rwlock_fini(struct c2_rwlock *lock)
{
	int rc;

	rc = pthread_rwlock_destroy(&lock->rw_lock);
	C2_ASSERT(rc == 0);
}

void c2_rwlock_write_lock(struct c2_rwlock *lock)
{
	int rc;

	rc = pthread_rwlock_wrlock(&lock->rw_lock);
	C2_ASSERT(rc == 0);
}

void c2_rwlock_write_unlock(struct c2_rwlock *lock)
{
	int rc;

	rc = pthread_rwlock_unlock(&lock->rw_lock);
	C2_ASSERT(rc == 0);
}

void c2_rwlock_read_lock(struct c2_rwlock *lock)
{
	int rc;

	rc = pthread_rwlock_rdlock(&lock->rw_lock);
	C2_ASSERT(rc == 0);
}

void c2_rwlock_read_unlock(struct c2_rwlock *lock)
{
	int rc;

	rc = pthread_rwlock_unlock(&lock->rw_lock);
	C2_ASSERT(rc == 0);
}

/** @} end of rwlock group */


/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
