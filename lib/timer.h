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

#ifndef __MERO_LIB_TIMER_H__
#define __MERO_LIB_TIMER_H__

#include "lib/types.h"
#include "lib/tlist.h"	   /* m0_tl */
#include "lib/mutex.h"	   /* m0_mutex */

/**
   @defgroup timer Generic timer manipulation

   Any timer should call m0_timer_init() function before any function. That
   init function does all initialization for this timer. After that, the
   m0_timer_start() function is called to start the timer. The timer callback
   function will be called repeatedly, if this is a repeatable timer. Function
   m0_timer_stop() is used to stop the timer, and m0_timer_fini() to destroy
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

   @note m0_timer_* functions should not be used in the timer callbacks.
   @{
 */

typedef	unsigned long (*m0_timer_callback_t)(unsigned long data);
struct m0_timer;

/**
   Timer type.
 */
enum m0_timer_type {
	M0_TIMER_SOFT,
	M0_TIMER_HARD
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
struct m0_timer_tid {
	pid_t		tt_tid;
	struct m0_tlink tt_linkage;
	uint64_t	tt_magic;
};

/**
   Timer locality.
   Used in userspace hard timers.
 */
struct m0_timer_locality {
	/**
	   Lock for tlo_tids
	 */
	struct m0_mutex tlo_lock;
	/**
	   List of thread ID's, associated with this locality
	 */
	struct m0_tl tlo_tids;
	/**
	   ThreadID of next thread for round-robin timer thread selection
	   in m0_timer_attach(). It is pointer to timer_tid structure.
	 */
	struct m0_timer_tid *tlo_rrtid;
};

/**
   Init the timer data structure.

   @param timer m0_timer structure
   @param type timer type (M0_TIMER_SOFT or M0_TIMER_HARD)
   @param expire absolute expiration time for timer. If this time is already
	  passed, then the timer callback will be executed immediatelly
	  after m0_timer_start().
   @param callback this callback will be triggered whem timer alarms.
   @param data data for the callback.
   @pre callback != NULL
   @post timer is not running
 */
M0_INTERNAL int m0_timer_init(struct m0_timer *timer, enum m0_timer_type type,
			      m0_time_t expire,
			      m0_timer_callback_t callback, unsigned long data);

/**
   Start a timer.

   @pre m0_timer_init() successfully called.
   @pre timer is not running
 */
M0_INTERNAL int m0_timer_start(struct m0_timer *timer);

/**
   Stop a timer.

   @pre m0_timer_init() successfully called.
   @pre timer is running
   @post timer is not running
   @post callback isn't running
 */
M0_INTERNAL int m0_timer_stop(struct m0_timer *timer);

/**
   Returns true iff the timer is running.
 */
M0_INTERNAL bool m0_timer_is_started(const struct m0_timer *timer);

/**
   Destroy the timer.

   @pre m0_timer_init() for this timer was succesfully called.
   @pre timer is not running.
 */
M0_INTERNAL int m0_timer_fini(struct m0_timer *timer);

/**
   Init timer locality.
 */
M0_INTERNAL void m0_timer_locality_init(struct m0_timer_locality *loc);

/**
   Fini timer locality.

   @pre m0_timer_locality_init() succesfully called.
   @pre locality is empty
 */
M0_INTERNAL void m0_timer_locality_fini(struct m0_timer_locality *loc);

/**
   Add current thread to the list of threads in locality.

   @pre m0_timer_locality_init() successfully called.
   @pre current thread is not attached to locality.
   @post current thread is attached to locality.
 */
M0_INTERNAL int m0_timer_thread_attach(struct m0_timer_locality *loc);

/**
   Remove current thread from the list of threads in locality.
   Current thread must be in this list.

   @pre m0_timer_locality_init() successfully called.
   @pre current thread is attached to locality.
   @post current thread is not attached to locality.
 */
M0_INTERNAL void m0_timer_thread_detach(struct m0_timer_locality *loc);

/**
   Attach hard timer to the given locality.
   This function will set timer signal number to signal number, associated
   with given locality, and thread ID for timer callback - it will be chosen
   from locality threads list in round-robin fashion.
   Therefore internal POSIX timer will be recreated.

   @pre m0_timer_init() successfully called.
   @pre m0_timer_locality_init() successfully called.
   @pre locality has some threads attached.
   @pre timer type is M0_TIMER_HARD
   @pre timer is not running.
 */
M0_INTERNAL int m0_timer_attach(struct m0_timer *timer,
				struct m0_timer_locality *loc);

/** @} end of timer group */
/* __MERO_LIB_TIMER_H__ */
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
