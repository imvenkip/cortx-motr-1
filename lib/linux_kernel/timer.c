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
 *                  Maxim Medved <Max_Medved@xyratex.com>
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
	struct c2_timer *timer = (struct c2_timer*)data;

	/* call the user callback */
	C2_ASSERT(timer->t_callback != NULL);
	timer->t_callback(timer->t_data);
	timer->t_running = false;
}

/**
   Init the timer data structure.
 */
int c2_timer_init(struct c2_timer *timer, enum c2_timer_type type,
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

C2_EXPORTED(c2_timer_init);

/**
   Start a timer.
 */
int c2_timer_start(struct c2_timer *timer)
{
	struct timespec ts = {
			.tv_sec  = c2_time_seconds(timer->t_expire),
			.tv_nsec = c2_time_nanoseconds(timer->t_expire)
		};
	if (timer->t_running)
		return -EBUSY;

	C2_ASSERT(timer->t_callback != NULL);

	timer->t_running = true;
	timer->t_timer.expires = timespec_to_jiffies(&ts);
	add_timer(&timer->t_timer);
	return 0;
}
C2_EXPORTED(c2_timer_start);

/**
   Stop a timer.
 */
int c2_timer_stop(struct c2_timer *timer)
{
	int rc = del_timer_sync(&timer->t_timer);

	timer->t_running = false;
	return rc;
}
C2_EXPORTED(c2_timer_stop);

/**
   Destroy the timer.
 */
int c2_timer_fini(struct c2_timer *timer)
{
	timer->t_running = false;
	timer->t_callback = NULL;
	timer->t_data = 0;
	return 0;
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
