/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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

#include "lib/timer.h"

#include "lib/misc.h"		/* M0_SET0 */
#include "lib/assert.h"		/* M0_PRE */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_LIB
#include "lib/trace.h"

/**
   @addtogroup timer

   Implementation of m0_timer.

   In userspace soft timer implementation, there is a timer thread running,
   which checks the expire time and trigger timer callback if needed.
   There is one timer thread for each timer.
 */

/**
   Function enum for timer_state_change() checks.
 */
enum timer_func {
	TIMER_INIT = 0,
	TIMER_FINI,
	TIMER_START,
	TIMER_STOP,
	TIMER_FUNC_NR
};

static bool timer_invariant(const struct m0_timer *timer)
{
	return timer != NULL &&
		M0_IN(timer->t_type, (M0_TIMER_HARD, M0_TIMER_SOFT)) &&
		M0_IN(timer->t_state, (TIMER_INITED, TIMER_RUNNING,
				       TIMER_STOPPED, TIMER_UNINIT));
}

/*
   This function called on every m0_timer_init/fini/start/stop.
   It checks the possibility of transition from the current state
   with a given function and if possible and changes timer state to a new state
   if it needed.
   @param dry_run if it is true, then timer state doesn't change.
   @return true if state can be changed with the given func, false otherwise
 */
static bool timer_state_change(struct m0_timer *timer, enum timer_func func,
		bool dry_run)
{
	enum m0_timer_state new_state;
	static enum m0_timer_state
		transition[TIMER_STATE_NR][TIMER_FUNC_NR] = {
			[TIMER_UNINIT] = {
				[TIMER_INIT]   = TIMER_INITED,
				[TIMER_FINI]   = TIMER_INVALID,
				[TIMER_START]  = TIMER_INVALID,
				[TIMER_STOP]   = TIMER_INVALID,
			},
			[TIMER_INITED] = {
				[TIMER_INIT]   = TIMER_INVALID,
				[TIMER_FINI]   = TIMER_UNINIT,
				[TIMER_START]  = TIMER_RUNNING,
				[TIMER_STOP]   = TIMER_INVALID,
			},
			[TIMER_RUNNING] = {
				[TIMER_INIT]   = TIMER_INVALID,
				[TIMER_FINI]   = TIMER_INVALID,
				[TIMER_START]  = TIMER_INVALID,
				[TIMER_STOP]   = TIMER_STOPPED,
			},
			[TIMER_STOPPED] = {
				[TIMER_INIT]   = TIMER_INVALID,
				[TIMER_FINI]   = TIMER_UNINIT,
				[TIMER_START]  = TIMER_INVALID,
				[TIMER_STOP]   = TIMER_INVALID,
			}
		};

	M0_PRE(timer_invariant(timer));

	new_state = func == TIMER_INIT ? TIMER_INITED :
		transition[timer->t_state][func];
	if (!dry_run && new_state != TIMER_INVALID)
		timer->t_state = new_state;
	return func == TIMER_INIT || new_state != TIMER_INVALID;
}

/**
   Init the timer data structure.
 */
M0_INTERNAL int m0_timer_init(struct m0_timer	       *timer,
			      enum m0_timer_type	type,
			      struct m0_timer_locality *loc,
			      m0_timer_callback_t	callback,
			      unsigned long		data)
{
	int rc;

	M0_PRE(callback != NULL);
	M0_PRE(M0_IN(type, (M0_TIMER_SOFT, M0_TIMER_HARD)));
	M0_PRE(timer != NULL);

	M0_SET0(timer);
	timer->t_type     = type;
	timer->t_expire	  = 0;
	timer->t_callback = callback;
	timer->t_data     = data;

	rc = m0_timer_ops[timer->t_type].tmr_init(timer, loc);
	timer_state_change(timer, TIMER_INIT, rc != 0);

	return M0_RC(rc);
}

/**
   Destroy the timer.
 */
M0_INTERNAL void m0_timer_fini(struct m0_timer *timer)
{
	M0_PRE(timer != NULL);
	M0_PRE(timer_state_change(timer, TIMER_FINI, true));

	m0_timer_ops[timer->t_type].tmr_fini(timer);
	timer_state_change(timer, TIMER_FINI, false);
}

/**
   Start a timer.
 */
M0_INTERNAL void m0_timer_start(struct m0_timer *timer,
				m0_time_t	 expire)
{
	M0_PRE(timer != NULL);
	M0_PRE(timer_state_change(timer, TIMER_START, true));

	timer->t_expire = expire;

	m0_timer_ops[timer->t_type].tmr_start(timer);
	timer_state_change(timer, TIMER_START, false);
}

/**
   Stop a timer.
 */
M0_INTERNAL void m0_timer_stop(struct m0_timer *timer)
{
	M0_PRE(timer != NULL);
	M0_PRE(timer_state_change(timer, TIMER_STOP, true));

	m0_timer_ops[timer->t_type].tmr_stop(timer);
	timer_state_change(timer, TIMER_STOP, false);
}

M0_INTERNAL bool m0_timer_is_started(const struct m0_timer *timer)
{
	return timer->t_state == TIMER_RUNNING;
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of timer group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
