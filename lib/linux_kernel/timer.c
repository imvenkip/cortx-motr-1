/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Huang Hua <hua_huang@xyratex.com>
 *                  Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 03/04/2011
 */

#include <linux/module.h>
#include <linux/jiffies.h>

#include "lib/cdefs.h"  /* C2_EXPORTED */
#include "lib/time.h"
#include "lib/timer.h"
#include "lib/assert.h"
#include "lib/thread.h"

/**
   @addtogroup timer

   <b>Implementation of c2_timer on top of Linux struct timer_list.</b>

   @{
*/

C2_INTERNAL void c2_timer_trampoline_callback(unsigned long data)
{
	struct c2_timer *timer = (struct c2_timer*)data;

	/* call the user callback */
	C2_ASSERT(timer->t_callback != NULL);
	c2_enter_awkward();
	timer->t_callback(timer->t_data);
	c2_exit_awkward();
	timer->t_running = false;
}

/**
   Init the timer data structure.
 */
C2_INTERNAL int c2_timer_init(struct c2_timer *timer, enum c2_timer_type type,
			      c2_time_t expire,
			      c2_timer_callback_t callback, unsigned long data)
{
	struct timer_list *tl;

	C2_PRE(callback != NULL);
	C2_PRE(type == C2_TIMER_SOFT || type == C2_TIMER_HARD);

	timer->t_type     = type;
	timer->t_expire   = expire;
	timer->t_running  = false;
	timer->t_callback = callback;
	timer->t_data     = data;

	tl = &timer->t_timer;
	init_timer(tl);
	tl->data = (unsigned long)timer;
	tl->function = c2_timer_trampoline_callback;
	return 0;
}


/**
   Start a timer.
 */
C2_INTERNAL int c2_timer_start(struct c2_timer *timer)
{
	c2_time_t now = c2_time_now();
	c2_time_t rem;
	struct timespec ts;

	if (timer->t_running)
		return -EBUSY;

	C2_ASSERT(timer->t_callback != NULL);

	if (timer->t_expire > now)
		rem = c2_time_sub(timer->t_expire, now);
	else
		c2_time_set(&rem, 0, 0);
	ts.tv_sec  = c2_time_seconds(rem);
	ts.tv_nsec = c2_time_nanoseconds(rem);
	timer->t_timer.expires = jiffies + timespec_to_jiffies(&ts);

	timer->t_running = true;
	add_timer(&timer->t_timer);
	return 0;
}

/**
   Stop a timer.
 */
C2_INTERNAL int c2_timer_stop(struct c2_timer *timer)
{
	int rc = del_timer_sync(&timer->t_timer);

	timer->t_running = false;
	return rc;
}

C2_INTERNAL bool c2_timer_is_started(const struct c2_timer *timer)
{
	return timer->t_running;
}

/**
   Destroy the timer.
 */
C2_INTERNAL int c2_timer_fini(struct c2_timer *timer)
{
	timer->t_running = false;
	timer->t_callback = NULL;
	timer->t_data = 0;
	return 0;
}

/** @} end of timer group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
