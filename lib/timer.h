/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 *                  Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 03/04/2011
 */

#pragma once

#ifndef __COLIBRI_LIB_TIMER_H__
#define __COLIBRI_LIB_TIMER_H__

#include "lib/types.h"
#include "lib/tlist.h"	   /* c2_tl */
#include "lib/mutex.h"	   /* c2_mutex */

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

   - Hard timer has better resolution and is driven by signal. The
     user-defined callback should take short time, should never block
     at any time. Also in user space it should be async-signal-safe
     (see signal(7)), in kernel space it can only take _irq spin-locks.
   - Soft timer creates separate thread to execute the user-defined
     callback for each timer. So the overhead is bigger than hard timer.
     The user-defined callback execution may take longer time and it will
     not impact other timers.

   @note c2_timer_* functions should not be used in the timer callbacks.
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
   Item of threads ID list in locality.
   Used in the implementation of userspace hard timer.
 */
struct c2_timer_tid {
	pid_t		tt_tid;
	struct c2_tlink tt_linkage;
	uint64_t	tt_magic;
};

/**
   Timer locality.
   Used in userspace hard timers.
 */
struct c2_timer_locality {
	/**
	   Lock for tlo_tids
	 */
	struct c2_mutex tlo_lock;
	/**
	   List of thread ID's, associated with this locality
	 */
	struct c2_tl tlo_tids;
	/**
	   ThreadID of next thread for round-robin timer thread selection
	   in c2_timer_attach(). It is pointer to timer_tid structure.
	 */
	struct c2_timer_tid *tlo_rrtid;
};

/**
   Init the timer data structure.

   @param timer c2_timer structure
   @param type timer type (C2_TIMER_SOFT or C2_TIMER_HARD)
   @param expire absolute expiration time for timer. If this time is already
	  passed, then the timer callback will be executed immediatelly
	  after c2_timer_start().
   @param callback this callback will be triggered whem timer alarms.
   @param data data for the callback.
   @pre callback != NULL
   @post timer is not running
 */
int c2_timer_init(struct c2_timer *timer, enum c2_timer_type type,
		  c2_time_t expire,
		  c2_timer_callback_t callback, unsigned long data);

/**
   Start a timer.

   @pre c2_timer_init() successfully called.
   @pre timer is not running
 */
int c2_timer_start(struct c2_timer *timer);

/**
   Stop a timer.

   @pre c2_timer_init() successfully called.
   @pre timer is running
   @post timer is not running
   @post callback isn't running
 */
int c2_timer_stop(struct c2_timer *timer);

/**
   Returns true iff the timer is running.
 */
bool c2_timer_is_started(const struct c2_timer *timer);

/**
   Destroy the timer.

   @pre c2_timer_init() for this timer was succesfully called.
   @pre timer is not running.
 */
int c2_timer_fini(struct c2_timer *timer);

/**
   Init timer locality.
 */
void c2_timer_locality_init(struct c2_timer_locality *loc);

/**
   Fini timer locality.

   @pre c2_timer_locality_init() succesfully called.
   @pre locality is empty
 */
void c2_timer_locality_fini(struct c2_timer_locality *loc);

/**
   Add current thread to the list of threads in locality.

   @pre c2_timer_locality_init() successfully called.
   @pre current thread is not attached to locality.
   @post current thread is attached to locality.
 */
int c2_timer_thread_attach(struct c2_timer_locality *loc);

/**
   Remove current thread from the list of threads in locality.
   Current thread must be in this list.

   @pre c2_timer_locality_init() successfully called.
   @pre current thread is attached to locality.
   @post current thread is not attached to locality.
 */
void c2_timer_thread_detach(struct c2_timer_locality *loc);

/**
   Attach hard timer to the given locality.
   This function will set timer signal number to signal number, associated
   with given locality, and thread ID for timer callback - it will be chosen
   from locality threads list in round-robin fashion.
   Therefore internal POSIX timer will be recreated.

   @pre c2_timer_init() successfully called.
   @pre c2_timer_locality_init() successfully called.
   @pre locality has some threads attached.
   @pre timer type is C2_TIMER_HARD
   @pre timer is not running.
 */
int c2_timer_attach(struct c2_timer *timer, struct c2_timer_locality *loc);

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
