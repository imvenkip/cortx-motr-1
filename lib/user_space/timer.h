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
 *		    Maxim Medved <Max_Medved@xyratex.com>
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
 * @addtogroup timer
 *
 * <b>User space timer.</b>
 * @{
 */

/**
 * Timer state.
 * @see timer_state_change()
 */
enum c2_timer_state {
	/** Not initialized. */
	TIMER_UNINIT = 0,
	/** Initialized. */
	TIMER_INITED,
	/** Timer is running. */
	TIMER_RUNNING,
	/** Timer is stopped */
	TIMER_STOPPED,
	/** Number of timer states */
	TIMER_STATE_NR,
	/** Invalid state */
	TIMER_INVALID = TIMER_STATE_NR
};

struct timer_tid;

struct c2_timer_locality {
	/**
	 * Lock for tlo_tids
	 */
	struct c2_mutex   tlo_lock;
	/**
	 * List of thread ID's, associated with this locality
	 */
	struct c2_tl	  tlo_tids;
	/**
	 * ThreadID of next thread for round-robin timer thread choosing
	 * in c2_timer_attach().
	 */
	struct timer_tid *tlo_rrtid;
};

struct c2_timer {
	/**
	 * Timer type: C2_TIMER_SOFT or C2_TIMER_HARD
	 */
	enum c2_timer_type t_type;

	/**
	 * Timer triggers this callback.
	 */
	c2_timer_callback_t t_callback;

	/**
	 * User data.
	 */
	unsigned long t_data;

	/**
	 * expire time in future of this timer.
	 */
	c2_time_t t_expire;

	/**
	 * working thread for soft timer
	 */
	struct c2_thread t_thread;

	/*
	 * semaphore for sleeping in c2_timer_working_thread().
	 */
	struct c2_semaphore t_sleep_sem;

	/**
	 * Target thread ID for hard timer callback.
	 * Initially set in c2_timer_init() to callers TID.
	 * Can be changed by calling c2_timer_attach().
	 */
	pid_t t_tid;

	/**
	 * Timer state.
	 * Used in state changes checking in hard timer.
	 * c2_timer_init() will set state to TIMER_INITED.
	 * c2_timer_fini()/start()/stop()/attach() will check current
	 * state using state transition matrix.
	 * If there is no transition from current state using given
	 * function, C2_ASSERT() will take `false' parameter.
	 */
	enum c2_timer_state t_state;

	/**
	 * Used in hard timer implementation.
	 * @see timer_hard_stop()
	 */
	struct c2_semaphore t_stop_sem;

	/**
	 * POSIX timer ID, returned by timer_create().
	 * Used in hard timer implementation.
	 * POSIX timer is creating in c2_timer_init() and c2_timer_attach().
	 * POSIX timer is deleting in c2_timer_attach() and c2_timer_fini().
	 */
	timer_t t_ptimer;
};

int c2_timers_init();
void c2_timers_fini();

/**
 * Init timer locality.
 *
 * @post locality is empty.
 */
void c2_timer_locality_init(struct c2_timer_locality *loc);

/**
 * Fini timer locality.
 *
 * @pre c2_timer_locality_init() succesfully called.
 * @pre locality is empty
 */
void c2_timer_locality_fini(struct c2_timer_locality *loc);

/**
 * Add current thread to the list of threads in locality.
 *
 * @pre c2_timer_locality_init() successfully called.
 * @pre current thread is not attached to locality.
 * @post current thread is attached to locality.
 * @return 0 means success.
 * @return -ENOMEM if there is no free memory for timer_tid structure.
 */
int c2_timer_thread_attach(struct c2_timer_locality *loc);

/**
 * Remove current thread from the list of threads in locality.
 * Current thread must be in this list.
 *
 * @pre c2_timer_locality_init() successfully called.
 * @pre current thread is attached to locality.
 * @post current thread is not attached to locality.
 */
void c2_timer_thread_detach(struct c2_timer_locality *loc);

/**
 * Attach hard timer to the given locality.
 * This function will set timer signal number to signal number, associated
 * with given locality, and thread ID for timer callback - it will be chosen
 * from locality threads list in round-robin fashion.
 * Therefore internal POSIX timer will be recreated.
 *
 * @pre c2_timer_init() successfully called.
 * @pre c2_timer_locality_init() successfully called.
 * @pre locality has some threads attached.
 * @pre timer type is C2_TIMER_HARD
 * @pre timer is not running.
 */
int c2_timer_attach(struct c2_timer *timer, struct c2_timer_locality *loc);

/**
 * Invariant for timer.
 *
 * @pre c2_timer_init() for this timer was succesfully called.
 * @pre c2_timer_fini() wasn't called for this timer.
 */
bool c2_timer_invariant(struct c2_timer *timer);

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
