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

#include "lib/time.h"
#include "lib/timer.h"
#include "lib/assert.h"
#include "lib/thread.h"

/**
   @addtogroup timer

   <b>Implementation of m0_timer on top of Linux struct timer_list.</b>

   @{
*/

M0_INTERNAL void m0_timer_trampoline_callback(unsigned long data)
{
	struct m0_timer *timer = (struct m0_timer*)data;

	/* call the user callback */
	M0_ASSERT(timer->t_callback != NULL);
	m0_enter_awkward();
	timer->t_callback(timer->t_data);
	m0_exit_awkward();
	timer->t_running = false;
}

/**
   Init the timer data structure.
 */
M0_INTERNAL int m0_timer_init(struct m0_timer	       *timer,
			      enum m0_timer_type	type,
			      struct m0_timer_locality *loc,
			      m0_timer_callback_t	callback,
			      unsigned long		data)
{
	struct timer_list *tl;

	M0_PRE(callback != NULL);
	M0_PRE(M0_IN(type, (M0_TIMER_SOFT, M0_TIMER_HARD)));

	timer->t_type     = type;
	timer->t_running  = false;
	timer->t_callback = callback;
	timer->t_data     = data;

	tl = &timer->t_timer;
	init_timer(tl);
	tl->data = (unsigned long)timer;
	tl->function = m0_timer_trampoline_callback;
	return 0;
}


/**
   Start a timer.
 */
M0_INTERNAL void m0_timer_start(struct m0_timer *timer,
				m0_time_t	 expire)
{
	struct timespec ts;
	m0_time_t       now = m0_time_now();

	M0_PRE(!timer->t_running);

	M0_ASSERT(timer->t_callback != NULL);

	timer->t_expire = expire;
	expire = expire > now ? m0_time_sub(expire, now) : 0;
	ts.tv_sec  = m0_time_seconds(expire);
	ts.tv_nsec = m0_time_nanoseconds(expire);
	timer->t_timer.expires = jiffies + timespec_to_jiffies(&ts);

	timer->t_running = true;
	add_timer(&timer->t_timer);
}

/**
   Stop a timer.
 */
M0_INTERNAL void m0_timer_stop(struct m0_timer *timer)
{
	/*
	 * This function returns whether it has deactivated
	 * a pending timer or not. It always successful.
	 */
	del_timer_sync(&timer->t_timer);

	timer->t_running = false;
}

M0_INTERNAL bool m0_timer_is_started(const struct m0_timer *timer)
{
	return timer->t_running;
}

/**
   Destroy the timer.
 */
M0_INTERNAL void m0_timer_fini(struct m0_timer *timer)
{
	timer->t_running = false;
	timer->t_callback = NULL;
	timer->t_data = 0;
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
