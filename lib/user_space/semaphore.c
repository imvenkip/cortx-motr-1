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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 03/11/2011
 */

#include "lib/semaphore.h"
#include "lib/assert.h"
#include "lib/errno.h"

/**
   @addtogroup semaphore

   Implementation of c2_semaphore on top of sem_t.

   @{
*/

C2_INTERNAL int c2_semaphore_init(struct c2_semaphore *semaphore,
				  unsigned value)
{
	return sem_init(&semaphore->s_sem, 0, value);
}

C2_INTERNAL void c2_semaphore_fini(struct c2_semaphore *semaphore)
{
	int rc;

	rc = sem_destroy(&semaphore->s_sem);
	C2_ASSERT(rc == 0);
}

C2_INTERNAL void c2_semaphore_down(struct c2_semaphore *semaphore)
{
	int rc;

	do
		rc = sem_wait(&semaphore->s_sem);
	while (rc == -1 && errno == EINTR);
}

C2_INTERNAL void c2_semaphore_up(struct c2_semaphore *semaphore)
{
	int rc;

	rc = sem_post(&semaphore->s_sem);
	C2_ASSERT(rc == 0);
}

C2_INTERNAL bool c2_semaphore_trydown(struct c2_semaphore *semaphore)
{
	int rc;

	do
		rc = sem_trywait(&semaphore->s_sem);
	while (rc == -1 && errno == EINTR);
	C2_ASSERT(rc == 0 || (rc == -1 && errno == EAGAIN));
	errno = 0;
	return rc == 0;
}

C2_INTERNAL unsigned c2_semaphore_value(struct c2_semaphore *semaphore)
{
	int rc;
	int result;

	rc = sem_getvalue(&semaphore->s_sem, &result);
	C2_ASSERT(rc == 0);
	C2_POST(result >= 0);
	return result;
}

C2_INTERNAL bool c2_semaphore_timeddown(struct c2_semaphore *semaphore,
					const c2_time_t abs_timeout)
{
	struct timespec ts = {
			.tv_sec  = c2_time_seconds(abs_timeout),
			.tv_nsec = c2_time_nanoseconds(abs_timeout)
		};
	int rc;

	do
		rc = sem_timedwait(&semaphore->s_sem, &ts);
	while (rc == -1 && errno == EINTR);
	C2_ASSERT(rc == 0 || (rc == -1 && errno == ETIMEDOUT));
	if (rc == -1 && errno == ETIMEDOUT)
		errno = 0;
	return rc == 0;
}

/** @} end of semaphore group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
