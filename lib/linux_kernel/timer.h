/* -*- C -*- */

#ifndef __COLIBRI_LIB_LINUX_KERNEL_TIMER_H__
#define __COLIBRI_LIB_LINUX_KERNEL_TIMER_H__

#include <linux/timer.h>

/**
   @addtogroup timer

   <b>Linux kernel timer.</a>
   @{
 */

struct c2_timer {
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
	   Helper field. Last time triggered the callback. This is used to have
	   higher resolution.
	struct c2_time t_last_triggered;
	 */

	struct timer_list t_timer;
};

/** @} end of timer group */

/* __COLIBRI_LIB_LINUX_KERNEL_TIMER_H__ */
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
