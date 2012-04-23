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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 03/11/2011
 */

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

void c2_semaphore_fini(struct c2_semaphore *semaphore)
{
}

void c2_semaphore_down(struct c2_semaphore *semaphore)
{
	down(&semaphore->s_sem);
}

int c2_semaphore_trydown(struct c2_semaphore *semaphore)
{
	return !down_trylock(&semaphore->s_sem);
}

void c2_semaphore_up(struct c2_semaphore *semaphore)
{
	up(&semaphore->s_sem);
}

unsigned c2_semaphore_value(struct c2_semaphore *semaphore)
{
	return semaphore->s_sem.count;
}

bool c2_semaphore_timeddown(struct c2_semaphore *semaphore,
			    const c2_time_t abs_timeout)
{
	c2_time_t nowtime;
	c2_time_t reltime;
	unsigned long reljiffies;
	struct timespec ts;

	nowtime = c2_time_now();
	/* same semantics as user_space semaphore: allow abs_time < now */
	if (c2_time_after(abs_timeout, nowtime))
		reltime = c2_time_sub(abs_timeout, nowtime);
	else
		reltime = 0;
	ts.tv_sec  = c2_time_seconds(reltime);
	ts.tv_nsec = c2_time_nanoseconds(reltime);
	reljiffies = timespec_to_jiffies(&ts);

	return down_timeout(&semaphore->s_sem, reljiffies) == 0;
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
