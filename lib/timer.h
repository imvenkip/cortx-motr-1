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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 03/04/2011
 */

#ifndef __COLIBRI_LIB_TIMER_H__
#define __COLIBRI_LIB_TIMER_H__

#include "lib/types.h"

/**
 * @defgroup timer Generic timer manipulation
 *
 * Any timer should call c2_timer_init() function before any function. That
 * init function does all initialization for this timer. After that, the
 * c2_timer_start() function is called to start the timer. The timer callback
 * function will be called repeatedly, if this is a repeatable timer. Function
 * c2_timer_stop() is used to stop the timer, and c2_timer_fini() to destroy
 * the timer after usage.
 *
 * User supplied callback function should be small, run and complete quickly.
 *
 * There are two types of timer: soft timer and hard timer. For Linux kernel
 * implementation, all timers are hard timer. For userspace implemenation,
 * soft timer and hard timer have different mechanism:
 *
 * - Hard timer has better resolution and is driven by signal. The
 *   user-defined callback should take short time, should never block
 *   at any time. Also in user space it should be async-signal-safe
 *   (see signal(7)), in kernel space it can only take _irq spin-locks.
 * - Soft timer creates separate thread to execute the user-defined
 *   callback for each timer. So the overhead is bigger than hard timer.
 *   The user-defined callback execution may take longer time and it will
 *   not impact other timers.
 *
 * @note c2_timer_* functions should not be used in the timer callbacks.
 * @{
 */

typedef	unsigned long (*c2_timer_callback_t)(unsigned long data);
struct c2_timer;

/**
 * Timer type.
 */
enum c2_timer_type {
	C2_TIMER_SOFT,
	C2_TIMER_HARD
};

#ifndef __KERNEL__
#include "lib/user_space/timer.h"
#else
#include "lib/linux_kernel/timer.h"
#endif


/**
 * Init the timer data structure.
 *
 * @param interval interval time from now.
 * @param repeat repeat count for this timer.
 * @param callback this callback will be triggered whem timer alarms.
 * @param data data for the callback.
 * @pre interval is not zero time
 * @pre repeat != 0
 * @pre callback != NULL
 * @post timer is not running
 *
 * @return 0 means success, other values mean error.
 */
int c2_timer_init(struct c2_timer *timer, enum c2_timer_type type,
		  c2_time_t interval, uint64_t repeat,
		  c2_timer_callback_t callback, unsigned long data);

/**
 * Start a timer.
 *
 * @pre c2_timer_init() successfully called.
 * @pre timer is not running
 * @return 0 means success, other values mean error.
 */
int c2_timer_start(struct c2_timer *timer);

/**
 * Stop a timer.
 *
 * @pre c2_timer_init() successfully called.
 * @pre timer is running
 * @post timer is not running
 * @post callback isn't running
 * @return 0 means success, other values mean error.
 */
int c2_timer_stop(struct c2_timer *timer);

/**
 * Destroy the timer.
 *
 * @pre c2_timer_init() for this timer was succesfully called.
 * @pre timer is not running.
 */
void c2_timer_fini(struct c2_timer *timer);

/** @} end of timer group */
/* __COLIBRI_LIB_TIMER_H__ */
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
