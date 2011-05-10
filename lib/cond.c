/* -*- C -*- */

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

void c2_cond_fini(struct c2_cond *cond)
{
	c2_chan_fini(&cond->c_chan);
}

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


void c2_cond_signal(struct c2_cond *cond, struct c2_mutex *mutex)
{
	C2_PRE(c2_mutex_is_locked(mutex));
	c2_chan_signal(&cond->c_chan);
}

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
