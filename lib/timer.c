/* -*- C -*- */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/mutex.h"
#include "lib/cond.h"
#include "lib/thread.h"
#include "lib/assert.h"

#include "lib/timer.h"

#include <stdio.h>
/**
   @addtogroup timer

   Implementation of c2_timer.

   In userspace timer implementation, there is a timer thread running,
   which checks the expire time and trigger timer callback if needed.
   There is one timer thread for each timer.

   @{
*/

void nothing(int unused)
{
}


static void c2_timer_working_thread(struct c2_timer *timer)
{
	c2_time_t next;
	c2_time_t now;
	c2_time_t rem;
	int rc;

	c2_time_set(&rem, 0, 0);
	/* capture this signal. It is used to wake this thread */
	signal(SIGUSR1, nothing);

	while (timer->t_left > 0) {
		c2_time_now(&now);
		if (c2_time_after(now, timer->t_expire))
			timer->t_expire = c2_time_add(now, timer->t_interval);

		next = c2_time_sub(timer->t_expire, now);
		while (timer->t_left > 0 && (rc = c2_nanosleep(next, &rem)) != 0) {
			next = rem;
		}
		if (timer->t_left == 0)
			break;
		timer->t_expire = c2_time_add(timer->t_expire, timer->t_interval);
		timer->t_callback(timer->t_data);
		if (timer->t_left == 0 || --timer->t_left == 0)
			break;
	}
}

/**
   Init the timer data structure.

   TODO: Currently hard timer is the same as soft timer. We will implement hard
   timer later.
 */
int c2_timer_init(struct c2_timer *timer, enum c2_timer_type type,
		  c2_time_t interval, uint64_t repeat,
		  c2_timer_callback_t callback, unsigned long data)
{
	C2_PRE(callback != NULL);
	C2_PRE(type == C2_TIMER_SOFT || type == C2_TIMER_HARD);

	C2_SET0(timer);
	timer->t_type     = type;
	timer->t_interval = interval;
	timer->t_repeat   = repeat;
	timer->t_left     = 0;
	timer->t_callback = callback;
	timer->t_data     = data;
	c2_time_set(&timer->t_expire, 0, 0);

	return 0;
}
C2_EXPORTED(c2_timer_init);

/**
   Start a timer.
 */
int c2_timer_start(struct c2_timer *timer)
{
	c2_time_t now;
	int rc;

	timer->t_expire = c2_time_add(c2_time_now(&now), timer->t_interval);
	timer->t_left = timer->t_repeat;

	rc = C2_THREAD_INIT(&timer->t_thread, struct c2_timer*, NULL,
			    &c2_timer_working_thread, timer);
	return rc;
}
C2_EXPORTED(c2_timer_start);

/**
   Stop a timer.
 */
int c2_timer_stop(struct c2_timer *timer)
{
	timer->t_left = 0;
	c2_time_set(&timer->t_expire, 0, 0);

        if (timer->t_thread.t_func != NULL) {
		c2_thread_signal(&timer->t_thread, SIGUSR1);
		c2_thread_join(&timer->t_thread);
		c2_thread_fini(&timer->t_thread);
        }
	return 0;
}
C2_EXPORTED(c2_timer_stop);

/**
   Destroy the timer.
 */
void c2_timer_fini(struct c2_timer *timer)
{
	C2_SET0(timer);
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
