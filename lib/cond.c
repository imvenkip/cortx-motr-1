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
 * Original creation date: 05/17/2010
 */

#include "lib/cond.h"
#include "lib/mutex.h"
#include "lib/assert.h"

/**
   @addtogroup cond

   Very simple implementation of condition variables on top of waiting
   channels.

   Self-explanatory.

   @see c2_chan

   @{
 */

void c2_cond_init(struct c2_cond *cond)
{
	c2_chan_init(&cond->c_chan);
}
C2_EXPORTED(c2_cond_init);

void c2_cond_fini(struct c2_cond *cond)
{
	c2_chan_fini(&cond->c_chan);
}
C2_EXPORTED(c2_cond_fini);

void c2_cond_wait(struct c2_cond *cond, struct c2_mutex *mutex)
{
	struct c2_clink clink;

	/*
	 * First, register the clink with the channel, *then* unlock the
	 * mutex. This guarantees that signals to the condition variable are not
	 * missed, because they are done under the mutex.
	 */

	C2_PRE(c2_mutex_is_locked(mutex));

	c2_clink_init(&clink, NULL);
	c2_clink_add(&cond->c_chan, &clink);
	c2_mutex_unlock(mutex);
	c2_chan_wait(&clink);
	c2_mutex_lock(mutex);
	c2_clink_del(&clink);
	c2_clink_fini(&clink);
}
C2_EXPORTED(c2_cond_wait);

bool c2_cond_timedwait(struct c2_cond *cond, struct c2_mutex *mutex,
		       const c2_time_t abs_timeout)
{
	struct c2_clink clink;
	bool retval;

	C2_PRE(c2_mutex_is_locked(mutex));

	c2_clink_init(&clink, NULL);
	c2_clink_add(&cond->c_chan, &clink);
	c2_mutex_unlock(mutex);
	retval = c2_chan_timedwait(&clink, abs_timeout);
	c2_mutex_lock(mutex);
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	return retval;
}
C2_EXPORTED(c2_cond_timedwait);

void c2_cond_signal(struct c2_cond *cond, struct c2_mutex *mutex)
{
	C2_PRE(c2_mutex_is_locked(mutex));
	c2_chan_signal(&cond->c_chan);
}
C2_EXPORTED(c2_cond_signal);

void c2_cond_broadcast(struct c2_cond *cond, struct c2_mutex *mutex)
{
	C2_PRE(c2_mutex_is_locked(mutex));
	c2_chan_broadcast(&cond->c_chan);
}

/** @} end of cond group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
