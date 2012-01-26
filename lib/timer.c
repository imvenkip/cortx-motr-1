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
 *                  Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 03/04/2011
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/**
   @todo hack, but without it timer_create(2) isn't declarated.
   in Makefile should be -iquote instead of -I
 */
#include </usr/include/time.h>	  /* timer_create */
#include <unistd.h>	  /* syscall */
#include <signal.h>	  /* timer_create */
#include <sys/syscall.h>  /* syscall */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/mutex.h"  /* c2_mutex */
#include "lib/thread.h" /* c2_thread */
#include "lib/assert.h" /* C2_ASSERT */
#include "lib/memory.h" /* C2_ALLOC_PTR */
#include "lib/errno.h"	/* errno */
#include "lib/time.h"	/* c2_time_t */
#include "lib/semaphore.h"  /* c2_semaphore */
#include "lib/timer.h"

/**
   @addtogroup timer

   Implementation of c2_timer.

   In userspace soft timer implementation, there is a timer thread running,
   which checks the expire time and trigger timer callback if needed.
   There is one timer thread for each timer.
 */

#ifndef C2_TIMER_DEBUG
#define C2_TIMER_DEBUG 1
#endif

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
	TIMER_ATTACH,
	TIMER_FUNC_NR
};

static c2_time_t zero_time;

/**
   Typed list of c2_timer_tid structures.
 */
C2_TL_DESCR_DEFINE(tid, "thread IDs", static, struct c2_timer_tid,
		tt_linkage, tt_magic,
		0x696c444954726d74,	/** ASCII "tmrTIDli" -
					  timer thread ID list item */
		0x686c444954726d74);	/** ASCII "tmrTIDlh" -
					  timer thread ID list head */
C2_TL_DEFINE(tid, static, struct c2_timer_tid);

/**
   gettid(2) implementation.
   Thread-safe, async-signal-safe.
 */
static pid_t gettid() {

	return syscall(SYS_gettid);
}

void c2_timer_locality_init(struct c2_timer_locality *loc)
{
	C2_PRE(loc != NULL);

	c2_mutex_init(&loc->tlo_lock);
	tid_tlist_init(&loc->tlo_tids);
	loc->tlo_rrtid = NULL;
}

void c2_timer_locality_fini(struct c2_timer_locality *loc)
{
	C2_PRE(loc != NULL);

	c2_mutex_fini(&loc->tlo_lock);
	tid_tlist_fini(&loc->tlo_tids);
}

static struct c2_timer_tid *locality_tid_find(struct c2_timer_locality *loc,
		pid_t tid)
{
	struct c2_timer_tid *tt;
	struct c2_timer_tid *result = NULL;

	C2_PRE(loc != NULL);

	c2_mutex_lock(&loc->tlo_lock);
	c2_tlist_for(&tid_tl, &loc->tlo_tids, tt) {
		if (tt->tt_tid == tid) {
			result = tt;
			break;
		}
	} c2_tlist_endfor;
	c2_mutex_unlock(&loc->tlo_lock);

	return result;
}

int c2_timer_thread_attach(struct c2_timer_locality *loc)
{
	pid_t tid;
	struct c2_timer_tid *tt;

	C2_PRE(loc != NULL);

	tid = gettid();
	C2_ASSERT(locality_tid_find(loc, tid) == NULL);

	C2_ALLOC_PTR(tt);
	if (tt == NULL)
		return -ENOMEM;

	tt->tt_tid = tid;

	c2_mutex_lock(&loc->tlo_lock);
	tid_tlink_init_at_tail(tt, &loc->tlo_tids);
	c2_mutex_unlock(&loc->tlo_lock);

	return 0;
}

void c2_timer_thread_detach(struct c2_timer_locality *loc)
{
	pid_t tid;
	struct c2_timer_tid *tt;

	C2_PRE(loc != NULL);

	tid = gettid();
	tt = locality_tid_find(loc, tid);
	C2_ASSERT(tt != NULL);

	c2_mutex_lock(&loc->tlo_lock);
	if (loc->tlo_rrtid == tt)
		loc->tlo_rrtid = NULL;
	tid_tlink_del_fini(tt);
	c2_mutex_unlock(&loc->tlo_lock);

	c2_free(tt);
}

/**
   Init POSIX timer, write it to timer->t_ptimer.
   Timer notification is signal TIMER_SIGNO to
   thread timer->t_tid.
 */
static int timer_posix_init(struct c2_timer *timer)
{
	struct sigevent se;
	timer_t ptimer;
	int rc;

	se.sigev_notify = SIGEV_THREAD_ID;
	se.sigev_signo = TIMER_SIGNO;
	se._sigev_un._tid = timer->t_tid;
	se.sigev_value.sival_ptr = timer;
	rc = timer_create(CLOCK_REALTIME, &se, &ptimer);
	/* preserve timer->t_ptimer if timer_create() isn't succeeded */
	if (rc == 0)
		timer->t_ptimer = ptimer;
	return rc;
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
	C2_ASSERT(rc == 0);
}

/**
   Run timer_settime() with given expire time (absolute).
   Return previous expiration time if old_expire != NULL.
 */
static void timer_posix_set(struct c2_timer *timer,
		c2_time_t expire, c2_time_t *old_expire)
{
	int		  rc;
	struct itimerspec ts;
	struct itimerspec ots;

	C2_PRE(timer != NULL);

	ts.it_interval.tv_sec = 0;
	ts.it_interval.tv_nsec = 0;
	ts.it_value.tv_sec = c2_time_seconds(expire);
	ts.it_value.tv_nsec = c2_time_nanoseconds(expire);

	rc = timer_settime(timer->t_ptimer, TIMER_ABSTIME, &ts, &ots);
	/*
	 * timer_settime() can only fail if timer->t_ptimer isn't valid
	 * timer ID or ts has invalid fields.
	 */
	C2_ASSERT(rc == 0);
	if (old_expire != NULL)
		c2_time_set(old_expire, ots.it_value.tv_sec,
				ots.it_value.tv_nsec);
}

/**
   Set up signal handler sighandler for given signo.
 */
static int timer_sigaction(int signo,
		void (*sighandler)(int, siginfo_t*, void*))
{
	struct sigaction sa;

	C2_SET0(&sa);
	sa.sa_sigaction = sighandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(signo, &sa, NULL) != 0)
		return errno;
	return 0;
}

/**
   Signal handler for all POSIX timers.
   si->si_value.sival_ptr contains pointer to corresponding c2_timer structure.
 */
static void timer_sighandler(int signo, siginfo_t *si, void *u_ctx)
{
	struct c2_timer *timer;

	C2_PRE(si != NULL && si->si_value.sival_ptr != 0);
	C2_PRE(si->si_code == SI_TIMER);
	C2_PRE(signo == TIMER_SIGNO);

	timer = si->si_value.sival_ptr;
#ifdef C2_TIMER_DEBUG
	C2_ASSERT(timer->t_tid == gettid());
#endif
	timer->t_callback(timer->t_data);
	c2_semaphore_up(&timer->t_stop_sem);
}

/**
   Soft timer working thread.
 */
static void c2_timer_working_thread(struct c2_timer *timer)
{
	if (!c2_semaphore_timeddown(&timer->t_sleep_sem, timer->t_expire))
		timer->t_callback(timer->t_data);
}

static bool timer_invariant(struct c2_timer *timer)
{
	C2_PRE(timer != NULL);

	if (!(timer->t_type == C2_TIMER_HARD ||
				timer->t_type == C2_TIMER_SOFT))
		return false;
	if (!(timer->t_state == TIMER_INITED ||
				timer->t_state == TIMER_RUNNING ||
				timer->t_state == TIMER_STOPPED ||
				timer->t_state == TIMER_UNINIT))
		return false;
	return true;
}

/*
   This function called on every c2_timer_init/fini/start/stop/attach.
   It checks the possibility of transition from the current state
   with a given function and if possible and changes timer state to a new state
   if it needed.
   @param dry_run if it is true, then timer state doesn't change.
   @return true if state can be changed with the given func, false otherwise
 */
static bool timer_state_change(struct c2_timer *timer, enum timer_func func,
		bool dry_run)
{
	enum c2_timer_state new_state;
	static enum c2_timer_state
		transition[TIMER_STATE_NR][TIMER_FUNC_NR] = {
			[TIMER_UNINIT] = {
				[TIMER_INIT]   = TIMER_INITED,
				[TIMER_FINI]   = TIMER_INVALID,
				[TIMER_START]  = TIMER_INVALID,
				[TIMER_STOP]   = TIMER_INVALID,
				[TIMER_ATTACH] = TIMER_INVALID
			},
			[TIMER_INITED] = {
				[TIMER_INIT]   = TIMER_INVALID,
				[TIMER_FINI]   = TIMER_UNINIT,
				[TIMER_START]  = TIMER_RUNNING,
				[TIMER_STOP]   = TIMER_INVALID,
				[TIMER_ATTACH] = TIMER_INITED
			},
			[TIMER_RUNNING] = {
				[TIMER_INIT]   = TIMER_INVALID,
				[TIMER_FINI]   = TIMER_INVALID,
				[TIMER_START]  = TIMER_INVALID,
				[TIMER_STOP]   = TIMER_STOPPED,
				[TIMER_ATTACH] = TIMER_INVALID
			},
			[TIMER_STOPPED] = {
				[TIMER_INIT]   = TIMER_INVALID,
				[TIMER_FINI]   = TIMER_UNINIT,
				[TIMER_START]  = TIMER_INVALID,
				[TIMER_STOP]   = TIMER_INVALID,
				[TIMER_ATTACH] = TIMER_INVALID
			}
		};

	C2_PRE(timer_invariant(timer));

	new_state = func == TIMER_INIT ? TIMER_INITED :
		transition[timer->t_state][func];
	if (!dry_run && new_state != TIMER_INVALID)
		timer->t_state = new_state;
	return func == TIMER_INIT || new_state != TIMER_INVALID;
}

/**
   Create POSIX timer for the given c2_timer.
 */
static int timer_hard_init(struct c2_timer *timer)
{
	int rc;

	timer->t_tid = gettid();
	rc = timer_posix_init(timer);
	if (rc == 0) {
		rc = c2_semaphore_init(&timer->t_stop_sem, 0);
		if (rc != 0)
			timer_posix_fini(timer->t_ptimer);
	}
	return rc;
}

/**
   Delete POSIX timer for the given c2_timer.
 */
static void timer_hard_fini(struct c2_timer *timer)
{
	c2_semaphore_fini(&timer->t_stop_sem);
	timer_posix_fini(timer->t_ptimer);
}

/**
   Start one-shot POSIX timer for the given c2_timer.
 */
static void timer_hard_start(struct c2_timer *timer)
{
	timer_posix_set(timer, timer->t_expire, NULL);
}

/**
   Stop POSIX timer for the given c2_timer and wait for termination
   of user-defined timer callback.
 */
static void timer_hard_stop(struct c2_timer *timer)
{
	c2_time_t expire;
	timer_posix_set(timer, zero_time, &expire);
	/* if timer was expired then wait until callback is finished */
	if (expire == zero_time)
		c2_semaphore_down(&timer->t_stop_sem);
}

static int timer_soft_init(struct c2_timer *timer)
{
	return c2_semaphore_init(&timer->t_sleep_sem, 0);
}

static void timer_soft_fini(struct c2_timer *timer)
{
	c2_semaphore_fini(&timer->t_sleep_sem);
}

/**
   Start soft timer thread.
 */
static int timer_soft_start(struct c2_timer *timer)
{
	return C2_THREAD_INIT(&timer->t_thread, struct c2_timer*, NULL,
			    &c2_timer_working_thread,
			    timer, "c2_timer_worker");
}

/**
   Stop soft timer thread and wait for its termination.
 */
static int timer_soft_stop(struct c2_timer *timer)
{
	int rc;

	c2_semaphore_up(&timer->t_sleep_sem);
	rc = c2_thread_join(&timer->t_thread);
	if (rc == 0)
		c2_thread_fini(&timer->t_thread);
	return rc;
}

int c2_timer_attach(struct c2_timer *timer, struct c2_timer_locality *loc)
{
	struct c2_timer_tid *tt;
	int rc;
	timer_t ptimer;
	pid_t old_tid;

	C2_PRE(loc != NULL);
	C2_PRE(timer != NULL);

	if (timer->t_type == C2_TIMER_SOFT)
		return 0;

	if (!timer_state_change(timer, TIMER_ATTACH, true))
		return -EINVAL;

	old_tid = timer->t_tid;
	c2_mutex_lock(&loc->tlo_lock);
	if (!tid_tlist_is_empty(&loc->tlo_tids)) {
		if (loc->tlo_rrtid == NULL)
			loc->tlo_rrtid = tid_tlist_head(&loc->tlo_tids);
		tt = loc->tlo_rrtid;
		timer->t_tid = tt->tt_tid;
		loc->tlo_rrtid = tid_tlist_next(&loc->tlo_tids, tt);
	}
	c2_mutex_unlock(&loc->tlo_lock);

	if (timer->t_tid != old_tid) {
		/*
		 * don't delete old posix timer
		 * until the new one can be created
		 */
		ptimer = timer->t_ptimer;
		rc = timer_posix_init(timer);
		if (rc == 0)
			timer_posix_fini(ptimer);
	} else {
		rc = 0;
	}

	timer_state_change(timer, TIMER_ATTACH, rc != 0);
	return rc;
}

/**
   Init the timer data structure.
 */
int c2_timer_init(struct c2_timer *timer, enum c2_timer_type type,
		  c2_time_t expire,
		  c2_timer_callback_t callback, unsigned long data)
{
	int rc;

	C2_PRE(callback != NULL);
	C2_PRE(type == C2_TIMER_SOFT || type == C2_TIMER_HARD);
	C2_PRE(timer != NULL);

	C2_SET0(timer);
	timer->t_type     = type;
	timer->t_expire	  = expire;
	timer->t_callback = callback;
	timer->t_data     = data;

	if (timer->t_type == C2_TIMER_HARD)
		rc = timer_hard_init(timer);
	else
		rc = timer_soft_init(timer);

	timer_state_change(timer, TIMER_INIT, rc != 0);

	return rc;
}

/**
   Destroy the timer.
 */
int c2_timer_fini(struct c2_timer *timer)
{
	if (!timer_state_change(timer, TIMER_FINI, true))
		return -EINVAL;

	if (timer->t_type == C2_TIMER_HARD)
		timer_hard_fini(timer);
	else
		timer_soft_fini(timer);

	C2_SET0(timer);
	return 0;
}

/**
   Start a timer.
 */
int c2_timer_start(struct c2_timer *timer)
{
	int rc = 0;

	if (!timer_state_change(timer, TIMER_START, true))
		return -EINVAL;

	if (timer->t_type == C2_TIMER_HARD)
		timer_hard_start(timer);
	else
		rc = timer_soft_start(timer);

	timer_state_change(timer, TIMER_START, rc != 0);
	return rc;
}

/**
   Stop a timer.
 */
int c2_timer_stop(struct c2_timer *timer)
{
	int rc = 0;

	if (!timer_state_change(timer, TIMER_STOP, true))
		return -EINVAL;

	if (timer->t_type == C2_TIMER_HARD)
		timer_hard_stop(timer);
	else
		rc = timer_soft_stop(timer);

	timer_state_change(timer, TIMER_STOP, rc != 0);
	return rc;
}

bool c2_timer_is_started(const struct c2_timer *timer)
{
	return timer->t_state == TIMER_RUNNING;
}

/**
   Init data structures for hard timer
 */
int c2_timers_init()
{
	timer_sigaction(TIMER_SIGNO, timer_sighandler);
	c2_time_set(&zero_time, 0, 0);
	return 0;
}

void c2_timers_fini()
{
}

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
