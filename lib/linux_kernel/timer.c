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
 * Original author: Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 03/04/2011
 */

#include <linux/module.h>

#include "lib/cdefs.h"  /* C2_EXPORTED */
#include "lib/time.h"
#include "lib/timer.h"
#include "lib/assert.h"

/**
   @addtogroup timer

   <b>Implementation of c2_timer on top of Linux struct timer_list.</b>

   @{
*/

void c2_timer_trampoline_callback(unsigned long data)
{
	struct c2_timer   *timer = (struct c2_timer*)data;
	struct timer_list *tl = &timer->t_timer;
	struct timespec    ts = {
			.tv_sec  = c2_time_seconds(timer->t_interval),
			.tv_nsec = c2_time_nanoseconds(timer->t_interval)
		};

	/* new expire */
	tl->expires += timespec_to_jiffies(&ts);

	/* call the user callback */
	C2_ASSERT(timer->t_callback != NULL);
	timer->t_callback(timer->t_data);

	/* detect the left count, decrease and add timer again if needed */
	if (timer->t_left > 0) {
		timer->t_left --;
		add_timer(tl);
	}
}

/**
   Init the timer data structure.
 */
void c2_timer_init(struct c2_timer *timer, enum c2_timer_type type,
		   c2_time_t interval, uint64_t repeat,
		   c2_timer_callback_t callback, unsigned long data)
{
	struct timer_list *tl;

	C2_PRE(callback != NULL);
	C2_PRE(type == C2_TIMER_SOFT || type == C2_TIMER_HARD);

	timer->t_type     = type;
	timer->t_interval = interval;
	timer->t_repeat   = repeat;
	timer->t_left     = 0;
	timer->t_callback = callback;
	timer->t_data     = data;

	tl = &timer->t_timer;
	init_timer(tl);
	tl->data = (unsigned long)timer;
	tl->function = c2_timer_trampoline_callback;
}

C2_EXPORTED(c2_timer_init);

/**
   Start a timer.
 */
int c2_timer_start(struct c2_timer *timer)
{
	struct timespec ts = {
			.tv_sec  = c2_time_seconds(timer->t_interval),
			.tv_nsec = c2_time_nanoseconds(timer->t_interval)
		};
	if (timer->t_left > 0) {
		return -EBUSY;
	}

	C2_ASSERT(timer->t_callback != NULL);

	timer->t_left = timer->t_repeat;
	if (timer->t_left > 0) {
		timer->t_timer.expires = jiffies +
				 timespec_to_jiffies(&ts);
		add_timer(&timer->t_timer);
	}
	return 0;
}
C2_EXPORTED(c2_timer_start);


/**
   Stop a timer.
 */
void c2_timer_stop(struct c2_timer *timer)
{
	timer->t_left = 0;
	(void)del_timer_sync(&timer->t_timer);
}
C2_EXPORTED(c2_timer_stop);

bool c2_timer_is_started(const struct c2_timer *timer)
{
	return timer->t_left > 0;
}
C2_EXPORTED(c2_timer_is_started);

/**
   Destroy the timer.
 */
void c2_timer_fini(struct c2_timer *timer)
{
	timer->t_repeat = 0;
	timer->t_left   = 0;
	timer->t_callback = NULL;
	timer->t_data = 0;
	return;
}
C2_EXPORTED(c2_timer_fini);

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
