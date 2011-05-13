/* -*- C -*- */

#ifndef __COLIBRI_LIB_TIMER_H__
#define __COLIBRI_LIB_TIMER_H__

#include "lib/types.h"

/**
   @defgroup timer Generic timer manipulation

   Any timer should call c2_timer_init() function before any function. That
   init function does all initialization for this timer. After that, the
   c2_timer_start() function is called to start the timer. The timer callback
   function will be called repeatedly, if this is a repeatable timer. Function
   c2_timer_stop() is used to stop the timer, and c2_timer_fini() to destroy
   the timer after usage.

   User supplied callback function should be small, run and complete quickly.

   There are two types of timer: soft timer and hard timer. For Linux kernel
   implementation, all timers are hard timer. For userspace implemenation,
   soft timer and hard timer have different mechanism:

   @li Hard timer has better resolution and is driven by signal. The
    user-defined callback should take short time and should never block
    at any time.
   @li Soft timer creates separate thread to execute the user-defined
    callback for each timer. So the overhead is bigger than hard timer.
    The user-defined callback execution may take longer time and it will
    not impact other timers.

   @todo currently, in userspace implementation, hard timer is the same
    as soft timer. Hard timer will be implemented later.
   @{
*/

typedef	unsigned long (*c2_timer_callback_t)(unsigned long data);
struct c2_timer;

/**
   Timer type.
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
   Init the timer data structure.

   @param interval interval time from now.
   @param repeat repeat count for this timer.
   @param callback this callback will be triggered whem timer alarms.
   @param data data for the callback.

   @return 0 means success, other values mean error.
 */
int c2_timer_init(struct c2_timer *timer, enum c2_timer_type type,
		  c2_time_t interval, uint64_t repeat,
		  c2_timer_callback_t callback, unsigned long data);

/**
   Start a timer.

   @pre c2_timer_init() successfully called.
   @return 0 means success, other values mean error.
 */
int c2_timer_start(struct c2_timer *timer);

/**
   Stop a timer.

   @pre c2_timer_init() successfully called.
   @return 0 means success, other values mean error.
 */
int c2_timer_stop(struct c2_timer *timer);

/**
   Destroy the timer.

   @pre c2_timer_init() successfully called.
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
