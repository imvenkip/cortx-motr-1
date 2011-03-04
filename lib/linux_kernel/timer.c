/* -*- C -*- */

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
	struct timer_list *tl = &timer->t_timer;

	/* new expire */
	tl->expires += timespec_to_jiffies(&timer->t_interval.ts);

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
int c2_timer_init(struct c2_timer *timer,
		  struct c2_time *interval, uint64_t repeat,
		  c2_timer_callback_t callback, unsigned long data)
{
	struct timer_list *tl = &timer->t_timer;

	timer->t_interval = *interval;
	timer->t_repeat = repeat;
	timer->t_left   = 0;
	timer->t_callback = callback;
	timer->t_data = data;

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
	if (timer->t_left > 0) {
		return -EBUSY;
	}

	C2_ASSERT(timer->t_callback != NULL);

	timer->t_left = timer->t_repeat;
	if (timer->t_left > 0) {
		timer->t_timer.expires = jiffies +
				 timespec_to_jiffies(&timer->t_interval.ts);
		add_timer(&timer->t_timer);
	}
	return 0;
}
C2_EXPORTED(c2_timer_start);


/**
   Stop a timer.
 */
int c2_timer_stop(struct c2_timer *timer)
{
	timer->t_left = 0;
	return del_timer_sync(&timer->t_timer);
}
C2_EXPORTED(c2_timer_stop);

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

/** @} end of mutex group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
