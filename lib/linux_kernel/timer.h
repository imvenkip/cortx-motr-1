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
	   Timer type: C2_TIMER_SOFT or C2_TIMER_HARD
	 */
	enum c2_timer_type t_type;

	/**
	   The interval to trigger the timer callback.
	 */
	c2_time_t t_interval;

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
