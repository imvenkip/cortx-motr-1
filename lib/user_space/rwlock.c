/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 12/02/2010
 */

#include "lib/cdefs.h"     /* NULL */
#include "assert.h"
#include "rwlock.h"

/**
   @addtogroup rwlock Read-write lock

   User space implementation is based on a posix rwlock
   (pthread_rwlock_init(3))

   @{
 */

C2_INTERNAL void c2_rwlock_init(struct c2_rwlock *lock)
{
	int rc;

	rc = pthread_rwlock_init(&lock->rw_lock, NULL);
	C2_ASSERT(rc == 0);
}

C2_INTERNAL void c2_rwlock_fini(struct c2_rwlock *lock)
{
	int rc;

	rc = pthread_rwlock_destroy(&lock->rw_lock);
	C2_ASSERT(rc == 0);
}

C2_INTERNAL void c2_rwlock_write_lock(struct c2_rwlock *lock)
{
	int rc;

	rc = pthread_rwlock_wrlock(&lock->rw_lock);
	C2_ASSERT(rc == 0);
}

C2_INTERNAL void c2_rwlock_write_unlock(struct c2_rwlock *lock)
{
	int rc;

	rc = pthread_rwlock_unlock(&lock->rw_lock);
	C2_ASSERT(rc == 0);
}

C2_INTERNAL void c2_rwlock_read_lock(struct c2_rwlock *lock)
{
	int rc;

	rc = pthread_rwlock_rdlock(&lock->rw_lock);
	C2_ASSERT(rc == 0);
}

C2_INTERNAL void c2_rwlock_read_unlock(struct c2_rwlock *lock)
{
	int rc;

	rc = pthread_rwlock_unlock(&lock->rw_lock);
	C2_ASSERT(rc == 0);
}
/*
bool c2_rwlock_read_trylock(struct c2_rwlock *lock)
{
	int rc;

	rc = pthread_rwlock_tryrdlock(&lock->rw_lock);
	C2_ASSERT(rc == EBUSY || rc == 0);
	return rc == 0;
}

bool c2_rwlock_write_trylock(struct c2_rwlock *lock)
{
	int rc;

	rc = pthread_rwlock_tryrdlock(&lock->rw_lock);
	C2_ASSERT(rc == EBUSY || rc == 0);
	return rc == 0;	
}
*/
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
