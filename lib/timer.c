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

#include <time.h>	  /* timer_create */
#include <signal.h>	  /* timer_create */
#include <unistd.h>	  /* syscall */
#include <sys/syscall.h>  /* syscall */

#include "lib/misc.h"   /* M0_SET0 */
#include "lib/mutex.h"  /* m0_mutex */
#include "lib/thread.h" /* m0_thread */
#include "lib/assert.h" /* M0_ASSERT */
#include "lib/memory.h" /* M0_ALLOC_PTR */
#include "lib/errno.h"	/* errno */
#include "lib/time.h"	/* m0_time_t */
#include "lib/semaphore.h"  /* m0_semaphore */
#include "lib/timer.h"

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
   Hard timer implementation uses TIMER_SIGNO signal
   for user-defined callback delivery.
 */
#define TIMER_SIGNO	SIGRTMIN

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

static const m0_time_t zero_time = M0_MKTIME(0, 0);
/** Clock source for M0_TIMER_HARD. @see timer_posix_set() */
static int	       clock_source_timer = -1;

/**
   Typed list of m0_timer_tid structures.
 */
M0_TL_DESCR_DEFINE(tid, "thread IDs", static, struct m0_timer_tid,
		tt_linkage, tt_magic,
		0x696c444954726d74,	/** ASCII "tmrTIDli" -
					  timer thread ID list item */
		0x686c444954726d74);	/** ASCII "tmrTIDlh" -
					  timer thread ID list head */
M0_TL_DEFINE(tid, static, struct m0_timer_tid);

/**
   gettid(2) implementation.
   Thread-safe, async-signal-safe.
 */
static pid_t gettid() {

	return syscall(SYS_gettid);
}

M0_INTERNAL void m0_timer_locality_init(struct m0_timer_locality *loc)
{
	M0_PRE(loc != NULL);

	m0_mutex_init(&loc->tlo_lock);
	tid_tlist_init(&loc->tlo_tids);
	loc->tlo_rrtid = NULL;
}

M0_INTERNAL void m0_timer_locality_fini(struct m0_timer_locality *loc)
{
	M0_PRE(loc != NULL);

	m0_mutex_fini(&loc->tlo_lock);
	tid_tlist_fini(&loc->tlo_tids);
}

static struct m0_timer_tid *locality_tid_find(struct m0_timer_locality *loc,
		pid_t tid)
{
	struct m0_timer_tid *tt;

	M0_PRE(loc != NULL);

	m0_mutex_lock(&loc->tlo_lock);
	m0_tl_for(tid, &loc->tlo_tids, tt) {
		if (tt->tt_tid == tid)
			break;
	} m0_tl_endfor;
	m0_mutex_unlock(&loc->tlo_lock);

	return tt;
}

static pid_t timer_locality_tid_next(struct m0_timer_locality *loc)
{
	pid_t tid = 0;

	if (loc != NULL) {
		m0_mutex_lock(&loc->tlo_lock);
		if (!tid_tlist_is_empty(&loc->tlo_tids)) {
			if (loc->tlo_rrtid == NULL)
				loc->tlo_rrtid = tid_tlist_head(&loc->tlo_tids);
			tid = loc->tlo_rrtid->tt_tid;
			loc->tlo_rrtid = tid_tlist_next(&loc->tlo_tids,
							loc->tlo_rrtid);
		}
		m0_mutex_unlock(&loc->tlo_lock);
	}
	return tid;
}

M0_INTERNAL int m0_timer_thread_attach(struct m0_timer_locality *loc)
{
	struct m0_timer_tid *tt;
	pid_t		     tid;
	int		     rc;

	M0_PRE(loc != NULL);

	tid = gettid();
	M0_ASSERT(locality_tid_find(loc, tid) == NULL);

	M0_ALLOC_PTR(tt);
	if (tt == NULL) {
		rc = -ENOMEM;
	} else {
		tt->tt_tid = tid;

		m0_mutex_lock(&loc->tlo_lock);
		tid_tlink_init_at_tail(tt, &loc->tlo_tids);
		m0_mutex_unlock(&loc->tlo_lock);

		rc = 0;
	}
	return M0_RC(rc);
}

M0_INTERNAL void m0_timer_thread_detach(struct m0_timer_locality *loc)
{
	pid_t tid;
	struct m0_timer_tid *tt;

	M0_PRE(loc != NULL);

	tid = gettid();
	tt = locality_tid_find(loc, tid);
	M0_ASSERT(tt != NULL);

	m0_mutex_lock(&loc->tlo_lock);
	if (loc->tlo_rrtid == tt)
		loc->tlo_rrtid = NULL;
	tid_tlink_del_fini(tt);
	m0_mutex_unlock(&loc->tlo_lock);

	m0_free(tt);
}

/**
   Init POSIX timer, write it to timer->t_ptimer.
   Timer notification is signal TIMER_SIGNO to
   thread timer->t_tid (or signal TIMER_SIGNO to the process
   if timer->t_tid == 0).
 */
static int timer_posix_init(struct m0_timer *timer)
{
	struct sigevent	se;
	timer_t		ptimer;
	int		rc;

	M0_SET0(&se);
	se.sigev_signo = TIMER_SIGNO;
	se.sigev_value.sival_ptr = timer;
	if (timer->t_tid == 0) {
		se.sigev_notify = SIGEV_SIGNAL;
	} else {
		se.sigev_notify = SIGEV_THREAD_ID;
		se._sigev_un._tid = timer->t_tid;
	}
	rc = timer_create(clock_source_timer, &se, &ptimer);
	/* preserve timer->t_ptimer if timer_create() isn't succeeded */
	if (rc == 0)
		timer->t_ptimer = ptimer;
	return M0_RC(rc);
}

/**
   Delete POSIX timer.
 */
static void timer_posix_fini(timer_t posix_timer)
{
	int rc;

	rc = timer_delete(posix_timer);
	/*
	 * timer_delete() can fail iff timer->t_ptimer isn't valid timer ID.
	 */
	M0_ASSERT_INFO(rc == 0, "rc = %d", rc);
}

static m0_time_t timer_time_to_realtime(m0_time_t expire)
{
	if (M0_CLOCK_SOURCE == M0_CLOCK_SOURCE_REALTIME_MONOTONIC &&
	    !M0_IN(expire, (M0_TIME_NEVER, 0, 1)))
		expire -= m0_time_monotonic_offset;
	return expire;
}

/**
   Run timer_settime() with given expire time (absolute).
   Return previous expiration time if old_expire != NULL.
 */
static void
timer_posix_set(struct m0_timer *timer, m0_time_t expire, m0_time_t *old_expire)
{
	int               rc;
	struct itimerspec ts;
	struct itimerspec ots;

	M0_PRE(timer != NULL);

	expire = timer_time_to_realtime(expire);

	ts.it_interval.tv_sec = 0;
	ts.it_interval.tv_nsec = 0;
	ts.it_value.tv_sec = m0_time_seconds(expire);
	ts.it_value.tv_nsec = m0_time_nanoseconds(expire);

	rc = timer_settime(timer->t_ptimer, TIMER_ABSTIME, &ts, &ots);
	/* timer_settime() can only fail if timer->t_ptimer isn't valid
	 * timer ID or ts has invalid fields. */
	M0_ASSERT_INFO(rc == 0, "rc = %d", rc);

	if (old_expire != NULL)
		*old_expire = m0_time(ots.it_value.tv_sec,
				      ots.it_value.tv_nsec);
}

/** Set up signal handler sighandler for given signo. */
static int timer_sigaction(int signo,
			   void (*sighandler)(int, siginfo_t*, void*))
{
	struct sigaction sa;

	M0_SET0(&sa);
	sigemptyset(&sa.sa_mask);
	if (sighandler == NULL) {
		sa.sa_handler = SIG_DFL;
	} else {
		sa.sa_sigaction = sighandler;
		sa.sa_flags = SA_SIGINFO;
	}

	return sigaction(signo, &sa, NULL) == 0 ? 0 : errno;
}

static void timer_callback_execute(struct m0_timer *timer)
{
	m0_enter_awkward();
	timer->t_callback(timer->t_data);
	m0_exit_awkward();
}

/**
   Signal handler for all POSIX timers.
   si->si_value.sival_ptr contains pointer to corresponding m0_timer structure.
 */
static void timer_sighandler(int signo, siginfo_t *si, void *u_ctx)
{
	struct m0_timer *timer;

	M0_PRE(si != NULL && si->si_value.sival_ptr != 0);
	M0_PRE(si->si_code == SI_TIMER);
	M0_PRE(signo == TIMER_SIGNO);

	timer = si->si_value.sival_ptr;
	M0_ASSERT_EX(ergo(timer->t_tid != 0, timer->t_tid == gettid()));
	timer_callback_execute(timer);
	m0_semaphore_up(&timer->t_stop_sem);
}

/**
   Soft timer working thread.
 */
static void timer_working_thread(struct m0_timer *timer)
{
	m0_semaphore_down(&timer->t_sleep_sem);
	if (!m0_semaphore_timeddown(&timer->t_sleep_sem, timer->t_expire))
		timer_callback_execute(timer);
	m0_semaphore_up(&timer->t_stop_sem);
}

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
   Create POSIX timer for the given m0_timer.
 */
static int timer_hard_init(struct m0_timer *timer,
			   struct m0_timer_locality *loc)
{
	int rc;

	timer->t_tid = timer_locality_tid_next(loc);
	rc = timer_posix_init(timer);
	if (rc == 0) {
		rc = m0_semaphore_init(&timer->t_stop_sem, 0);
		if (rc != 0)
			timer_posix_fini(timer->t_ptimer);
	}
	return M0_RC(rc);
}

/**
   Delete POSIX timer for the given m0_timer.
 */
static void timer_hard_fini(struct m0_timer *timer)
{
	m0_semaphore_fini(&timer->t_stop_sem);
	timer_posix_fini(timer->t_ptimer);
}

/**
   Start one-shot POSIX timer for the given m0_timer.
 */
static void timer_hard_start(struct m0_timer *timer)
{
	/* expire = 0 will not arm the timer, so use 1ns in this case */
	timer_posix_set(timer,
			timer->t_expire == 0 ? 1 : timer->t_expire, NULL);
}

/**
   Stop POSIX timer for the given m0_timer and wait for termination
   of user-defined timer callback.
 */
static void timer_hard_stop(struct m0_timer *timer)
{
	m0_time_t expire;
	timer_posix_set(timer, zero_time, &expire);
	/* if timer was expired then wait until callback is finished */
	if (expire == zero_time)
		m0_semaphore_down(&timer->t_stop_sem);
}

static int timer_soft_initfini(struct m0_timer *timer, bool init)
{
	int rc;

	if (!init)
		goto fini;
	rc = m0_semaphore_init(&timer->t_sleep_sem, 0);
	if (rc != 0)
		goto err;
	rc = m0_semaphore_init(&timer->t_stop_sem, 0);
	if (rc != 0)
		goto fini_sleep_sem;
	rc = M0_THREAD_INIT(&timer->t_thread, struct m0_timer*, NULL,
			    &timer_working_thread, timer, "timer_thread");
	if (rc != 0)
		goto fini_stop_sem;
	return 0;

fini:
	rc = m0_thread_join(&timer->t_thread);
	/*
	 * There is something wrong with timers logic
	 * if thread can't be joined.
	 */
	M0_ASSERT_INFO(rc == 0, "rc = %d", rc);
	m0_thread_fini(&timer->t_thread);
fini_stop_sem:
	m0_semaphore_fini(&timer->t_stop_sem);
fini_sleep_sem:
	m0_semaphore_fini(&timer->t_sleep_sem);
err:
	return M0_RC(rc);
}

static int timer_soft_init(struct m0_timer *timer,
			   struct m0_timer_locality *loc)
{
	return timer_soft_initfini(timer, true);
}

static void timer_soft_fini(struct m0_timer *timer)
{
	int rc;

	m0_semaphore_up(&timer->t_sleep_sem);
	rc = timer_soft_initfini(timer, false);
	M0_ASSERT_INFO(rc == 0, "rc = %d", rc);
}

static void timer_soft_start(struct m0_timer *timer)
{
	m0_semaphore_up(&timer->t_sleep_sem);
}

/**
   Stop soft timer thread and wait for its termination.
 */
static void timer_soft_stop(struct m0_timer *timer)
{
	m0_semaphore_up(&timer->t_sleep_sem);
	m0_semaphore_down(&timer->t_stop_sem);
}

static const struct m0_timer_ops timer_ops[] = {
	[M0_TIMER_SOFT] = {
		.tmr_init  = timer_soft_init,
		.tmr_fini  = timer_soft_fini,
		.tmr_start = timer_soft_start,
		.tmr_stop  = timer_soft_stop,
	},
	[M0_TIMER_HARD] = {
		.tmr_init  = timer_hard_init,
		.tmr_fini  = timer_hard_fini,
		.tmr_start = timer_hard_start,
		.tmr_stop  = timer_hard_stop,
	},
};

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

	rc = timer_ops[timer->t_type].tmr_init(timer, loc);
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

	timer_ops[timer->t_type].tmr_fini(timer);
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

	timer_ops[timer->t_type].tmr_start(timer);
	timer_state_change(timer, TIMER_START, false);
}

/**
   Stop a timer.
 */
M0_INTERNAL void m0_timer_stop(struct m0_timer *timer)
{
	M0_PRE(timer != NULL);
	M0_PRE(timer_state_change(timer, TIMER_STOP, true));

	timer_ops[timer->t_type].tmr_stop(timer);
	timer_state_change(timer, TIMER_STOP, false);
}

M0_INTERNAL bool m0_timer_is_started(const struct m0_timer *timer)
{
	return timer->t_state == TIMER_RUNNING;
}

/**
   Init data structures for hard timer
 */
M0_INTERNAL int m0_timers_init(void)
{
	timer_sigaction(TIMER_SIGNO, timer_sighandler);

	switch (M0_CLOCK_SOURCE) {
	case M0_CLOCK_SOURCE_GTOD:
		clock_source_timer = CLOCK_REALTIME;
		break;
	case M0_CLOCK_SOURCE_REALTIME_MONOTONIC:
		clock_source_timer = CLOCK_MONOTONIC;
		break;
	case M0_CLOCK_SOURCE_REALTIME:
	case M0_CLOCK_SOURCE_MONOTONIC:
	case M0_CLOCK_SOURCE_MONOTONIC_RAW:
		clock_source_timer = M0_CLOCK_SOURCE;
		break;
	default:
		M0_IMPOSSIBLE("Invalid clock source for timer");
	}
	return 0;
}

M0_INTERNAL void m0_timers_fini(void)
{
	clock_source_timer = -1;
	timer_sigaction(TIMER_SIGNO, NULL);
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
