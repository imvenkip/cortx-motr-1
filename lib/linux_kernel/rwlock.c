/* -*- C -*- */

#include "lib/cdefs.h"  /* C2_EXPORTED */
#include "lib/rwlock.h"
#include "lib/assert.h"

/**
   @addtogroup rwlock Read-write lock

   @{
 */

void c2_rwlock_init(struct c2_rwlock *lock)
{
	init_rwsem(&lock->rw_sem);
}
C2_EXPORTED(c2_rwlock_init);

void c2_rwlock_fini(struct c2_rwlock *lock)
{
	C2_ASSERT(!rwsem_is_locked(&lock->rw_sem));
}
C2_EXPORTED(c2_rwlock_fini);

void c2_rwlock_write_lock(struct c2_rwlock *lock)
{
	down_write(&lock->rw_sem);
}
C2_EXPORTED(c2_rwlock_write_lock);

void c2_rwlock_write_unlock(struct c2_rwlock *lock)
{
	up_write(&lock->rw_sem);
}
C2_EXPORTED(c2_rwlock_write_unlock);

void c2_rwlock_read_lock(struct c2_rwlock *lock)
{
	down_read(&lock->rw_sem);
}
C2_EXPORTED(c2_rwlock_read_lock);

void c2_rwlock_read_unlock(struct c2_rwlock *lock)
{
	up_read(&lock->rw_sem);
}
C2_EXPORTED(c2_rwlock_read_unlock);

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
