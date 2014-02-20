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
 *		    Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 03/04/2011
 */

#pragma once

#ifndef __MERO_LIB_USER_SPACE_TIMER_H__
#define __MERO_LIB_USER_SPACE_TIMER_H__

#include "lib/time.h"      /* m0_time_t */
#include "lib/thread.h"    /* m0_thread */
#include "lib/semaphore.h" /* m0_semaphore */

/**
   @addtogroup timer

   <b>User space timer.</b>
   @{
 */

/**
   Timer state.
   @see timer_state_change()
 */
enum m0_timer_state {
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

struct m0_timer {
	/**
	   Timer type: M0_TIMER_SOFT or M0_TIMER_HARD
	 */
	enum m0_timer_type t_type;

	/**
	   Timer triggers this callback.
	 */
	m0_timer_callback_t t_callback;

	/**
	   User data.
	 */
	unsigned long t_data;

	/**
	   expire time in future of this timer.
	 */
	m0_time_t t_expire;

	/**
	   working thread for soft timer
	 */
	struct m0_thread t_thread;

	/*
	   semaphore for sleeping in m0_timer_working_thread().
	 */
	struct m0_semaphore t_sleep_sem;

	/**
	   Target thread ID for hard timer callback.
	   If it is 0 then signal will be sent to the process
	   but not to any specific thread.
	 */
	pid_t t_tid;

	/**
	   Timer state.
	   Used in state changes checking in hard timer.
	   m0_timer_init() will set state to TIMER_INITED.
	   m0_timer_fini()/start()/stop() will check current
	   state using state transition matrix.
	   If there is no transition from current state using given
	   function, M0_ASSERT() will take `false' parameter.
	 */
	enum m0_timer_state t_state;

	/**
	   Used in hard timer implementation.
	   @see timer_hard_stop()
	 */
	struct m0_semaphore t_stop_sem;

	/**
	   POSIX timer ID, returned by timer_create().
	   Used in hard timer implementation.
	   POSIX timer is creating in m0_timer_init().
	   POSIX timer is deleting in m0_timer_fini().
	 */
	timer_t t_ptimer;
};

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

   The signal for M0_TIMER_HARD timers will be delivered to a thread
   from the locality.
 */
struct m0_timer_locality {
	/** Lock for tlo_tids */
	struct m0_mutex tlo_lock;
	/** List of thread ID's, associated with this locality */
	struct m0_tl tlo_tids;
	/** ThreadID of next thread for round-robin timer thread selection */
	struct m0_timer_tid *tlo_rrtid;
};

/* Timer operations */
struct m0_timer_ops {
	int (*tmr_init)(struct m0_timer *timer, struct m0_timer_locality *loc);
	void (*tmr_fini)(struct m0_timer *timer);
	void (*tmr_start)(struct m0_timer *timer);
	void (*tmr_stop)(struct m0_timer *timer);
};

M0_INTERNAL int m0_timers_init(void);
M0_INTERNAL void m0_timers_fini(void);

/** @} end of timer group */

/* __MERO_LIB_USER_SPACE_TIMER_H__ */
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
