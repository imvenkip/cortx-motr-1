/* -*- C -*- */

#include <linux/module.h>

#include "lib/cdefs.h"  /* C2_EXPORTED */
#include "lib/rwlock.h"
#include "lib/assert.h"

/**
   @defgroup rwlock Read-write lock
   @{
 */

void c2_rwlock_init(struct c2_rwlock *lock)
{
	rwlock_init(&lock->m_rwlock);
}
C2_EXPORTED(c2_rwlock_init);

void c2_rwlock_fini(struct c2_rwlock *lock)
{
}
C2_EXPORTED(c2_rwlock_fini);

void c2_rwlock_write_lock(struct c2_rwlock *lock)
{
	write_lock(&lock->m_rwlock);
}
C2_EXPORTED(c2_rwlock_write_lock);

void c2_rwlock_write_unlock(struct c2_rwlock *lock)
{
	write_unlock(&lock->m_rwlock);
}
C2_EXPORTED(c2_rwlock_write_unlock);

void c2_rwlock_read_lock(struct c2_rwlock *lock)
{
	read_lock(&lock->m_rwlock);
}
C2_EXPORTED(c2_rwlock_read_lock);

void c2_rwlock_read_unlock(struct c2_rwlock *lock)
{
	read_unlock(&lock->m_rwlock);
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
