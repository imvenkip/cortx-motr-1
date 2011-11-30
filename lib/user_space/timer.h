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

#ifndef __COLIBRI_LIB_USER_SPACE_TIMER_H__
#define __COLIBRI_LIB_USER_SPACE_TIMER_H__

#include "lib/time.h"      /* c2_time_t */
#include "lib/thread.h"    /* c2_thread */
#include "lib/mutex.h"	   /* c2_mutex */
#include "lib/tlist.h"	   /* c2_tl */
#include "lib/semaphore.h" /* c2_semaphore */

/**
   @addtogroup timer

   <b>User space timer.</b>
   @{
*/

/**
   Timer can be in one of this states.
   Transition matrix for states:
			init	fini	start	stop	attach
   (0) TIMER_UNINIT	1	-	-	-	-
   (1) TIMER_INITED	-	0	2	-	1
   (2) TIMER_RUNNING	-	-	-	1	-
 */
enum c2_timer_state {
	/* Not initialized. */
	TIMER_UNINIT= 0,
	/* Initialized. */
	TIMER_INITED,
	/* Timer is running. */
	TIMER_RUNNING,
	/* Maximum value in enum */
	TIMER_STATE_MAX
};

struct timer_tid;

struct c2_timer_locality {
	/**
	   Lock for tlo_tids
	 */
	struct c2_mutex   tlo_lock;
	/**
	   List of thread ID's, associated with this locality
	 */
	struct c2_tl	  tlo_tids;
	/**
	   ThreadID of next thread for round-robin timer thread choosing
	   in c2_timer_attach().
	 */
	struct timer_tid *tlo_rrtid;

	/**
	   Signal number for this locality.
	   Will be assigned to every timer, attached to this locality.
	 */
	int tlo_signo;
};

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

	/**
	   expire time in future of this timer.
	 */
	c2_time_t t_expire;

	/**
	   working thread for soft timer
	 */
	struct c2_thread t_thread;

	/**
	   Target thread ID for hard timer callback.
	   Initially set in c2_timer_init() to callers TID.
	   Can be changed by calling c2_timer_attach().
	 */
	pid_t t_tid;

	/**
	   Signal number for POSIX timer.
	   Used in hard timer implementation.
	 */
	int t_signo;

	/**
	   Timer state.
	   Used in state changes checking in hard timer.
	 */
	enum c2_timer_state t_state;

	/**
	   Used in hard timer implementation.
	 */
	sig_atomic_t t_stopping;

	/**
	   Used in hard timer implementation.
	 */
	struct c2_semaphore t_stop_sem;

	/**
	   POSIX timer ID, returned by timer_create().
	   Used in hard timer implementation.
	 */
	timer_t t_ptimer;
};

int c2_timers_init();
void c2_timers_fini();

/**
   Init timer locality.
   Locality is empty after creation.
 */
void c2_timer_locality_init(struct c2_timer_locality *loc);

/**
   Finish timer locality.
   Locality must be empty.
 */
void c2_timer_locality_fini(struct c2_timer_locality *loc);

/**
   Add current thread to the list of threads in locality.
   Can return -ENOMEM if there is no free memory for timer_tid structure.
 */
int c2_timer_thread_attach(struct c2_timer_locality *loc);

/**
   Remove current thread from the list of threads in locality.
   Current thread must be in this list.
 */
void c2_timer_thread_detach(struct c2_timer_locality *loc);

void c2_timer_attach(struct c2_timer *timer, struct c2_timer_locality *loc);

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
