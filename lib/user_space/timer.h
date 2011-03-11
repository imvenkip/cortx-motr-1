/* -*- C -*- */

#ifndef __COLIBRI_LIB_USER_SPACE_TIMER_H__
#define __COLIBRI_LIB_USER_SPACE_TIMER_H__

#include "lib/time.h"
#include "lib/thread.h"

/**
   @addtogroup timer

   <b>User space timer.</b>
   @{
*/

struct c2_timer {
	/**
	   Timer type: C2_TIMER_SOFT or C2_TIMER_HARD
	 */
	enum c2_timer_type t_type;

	/**
	   The interval to trigger the timer callback.
	 */
	struct c2_time t_interval;

	/**
	   the repeat count for this timer.

	   Initial value of 0XFFFFFFFFFFFFFFFF means the timer will be triggered
	   infinitely before wrapping.
	 */
	uint64_t       t_repeat;

	/**
	   the left count for this timer.

	   This value will be decreased everytime a timeout happens.
	   If this value reaches zero, time is stopped/unarmed.
	   The initial value of @t_left is equal to @t_repeat.
	 */
	uint64_t       t_left;

	/**
	   Timer triggers this callback.
	 */
	c2_timer_callback_t t_callback;

	/**
	   User data.
	 */
	unsigned long t_data;

	/**
	   expire time in future of this timer.
	 */
	struct c2_time t_expire;

	/**
	   working thread
	 */
	struct c2_thread t_thread;
};



/** @} end of timer group */

/* __COLIBRI_LIB_USER_SPACE_TIMER_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
