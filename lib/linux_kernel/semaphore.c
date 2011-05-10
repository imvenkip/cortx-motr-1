/* -*- C -*- */

#include <linux/semaphore.h>

#include "lib/semaphore.h"
#include "lib/assert.h"
#include "lib/cdefs.h"     /* C2_EXPORTED */
#include "lib/time.h"

/**
   @addtogroup semaphore

   <b>Implementation of c2_semaphore on top of Linux struct semaphore.</b>

   @{
*/

int c2_semaphore_init(struct c2_semaphore *semaphore, unsigned value)
{
	sema_init(&semaphore->s_sem, value);
	return 0;
}
C2_EXPORTED(c2_semaphore_init);

void c2_semaphore_fini(struct c2_semaphore *semaphore)
{
}
C2_EXPORTED(c2_semaphore_fini);

void c2_semaphore_down(struct c2_semaphore *semaphore)
{
	down(&semaphore->s_sem);
}
C2_EXPORTED(c2_semaphore_down);

int c2_semaphore_trydown(struct c2_semaphore *semaphore)
{
	return !down_trylock(&semaphore->s_sem);
}
C2_EXPORTED(c2_semaphore_trydown);

void c2_semaphore_up(struct c2_semaphore *semaphore)
{
	up(&semaphore->s_sem);
}
C2_EXPORTED(c2_semaphore_up);

unsigned c2_semaphore_value(struct c2_semaphore *semaphore)
{
	return semaphore->s_sem.count;
}
C2_EXPORTED(c2_semaphore_value);

bool c2_semaphore_timeddown(struct c2_semaphore *semaphore,
			    const c2_time_t abs_timeout)
{
	/* not implemented, until kernel lib/time.h supports a notion of current
	   time. */
	return -ENOSYS;
}
C2_EXPORTED(c2_semaphore_timeddown);

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
