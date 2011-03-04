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
   @{
*/

typedef	unsigned long (*c2_timer_callback_t)(unsigned long data);
struct c2_timer;

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
int c2_timer_init(struct c2_timer *timer,
		  struct c2_time *interval, uint64_t repeat,
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
