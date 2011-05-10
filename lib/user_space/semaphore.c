/* -*- C -*- */

#include "lib/semaphore.h"
#include "lib/assert.h"
#include "lib/errno.h"

/**
   @addtogroup semaphore

   Implementation of c2_semaphore on top of sem_t.

   @{
*/

int c2_semaphore_init(struct c2_semaphore *semaphore, unsigned value)
{
	return sem_init(&semaphore->s_sem, 0, value);
}

void c2_semaphore_fini(struct c2_semaphore *semaphore)
{
	int rc;

	rc = sem_destroy(&semaphore->s_sem);
	C2_ASSERT(rc == 0);
}

void c2_semaphore_down(struct c2_semaphore *semaphore)
{
	int rc;

	do
		rc = sem_wait(&semaphore->s_sem);
	while (rc == -1 && errno == EINTR);
}

void c2_semaphore_up(struct c2_semaphore *semaphore)
{
	int rc;

	rc = sem_post(&semaphore->s_sem);
	C2_ASSERT(rc == 0);
}

int c2_semaphore_trydown(struct c2_semaphore *semaphore)
{
	int rc;

	do
		rc = sem_trywait(&semaphore->s_sem);
	while (rc == -1 && errno == EINTR);
	C2_ASSERT(rc == 0 || (rc == -1 && errno == EAGAIN));
	errno = 0;
	return rc == 0;
}

unsigned c2_semaphore_value(struct c2_semaphore *semaphore)
{
	int rc;
	int result;

	rc = sem_getvalue(&semaphore->s_sem, &result);
	C2_ASSERT(rc == 0);
	C2_POST(result >= 0);
	return result;
}

bool c2_semaphore_timeddown(struct c2_semaphore *semaphore,
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
