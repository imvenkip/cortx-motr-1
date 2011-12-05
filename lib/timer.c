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
#include "lib/atomic.h" /* c2_atomic64 */
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

   @{
*/
/**
 * Hard timer states
 *
 *	+-----------------+          +-------------+	       +---------+
 *	|		  |-- init >>|		   |-- start >>|	 |
 *	| not initialized |	     | initialized |           | running |
 *	|		  |<< fini --|		   |<< stop ---|	 |
 *	+-----------------+	     +-------------+	       +---------+
 *				       v	 ^
 *				       + attach -+
 */
/**
 * mini-DLD for timer functions
 *
 * signal handler
 *	if (t_stopping)
 *		timer_gettime()
 *		if current_timer_interval is magic
 *			timer_settime(disable)
 *			sem_post(t_stop_sem)
 *		return
 *	t_callback(t_data);
 *	t_left--;
 *	if (t_left == 0)
 *		return;
 *	timer_settime(interval = t_interval, count = 1)
 *
 * init
 *	t_stopping = false
 *	t_tid = gettid()
 *	t_signo = signo with round-robin choosing
 *	init t_stop_sem
 *	timer_create(signo = t_signo, target tid = t_tid())
 * fini
 *	fini t_stop_sem
 *	timer_delete()
 * start
 *	if (t_repeat != 0)
 *		t_left = t_repeat
 *		timer_settime(interval = t_interval, count = 1)
 * stop
 *	t_stopping = true
 *	timer_settime(expireation = now, interval = magic interval)
 *	sem_wait(t_stop_sem)
 *	t_stopping = false
 * attach
 *	timer_delete()
 *	choose t_tid
 *	timer_create(signo = t_signo, target tid = t_tid)
 */
/**
 * c2_timer access table for timer functions (R - read, W - write)
 *
 * c2_timer field	init	fini	start	stop	attach	sighandler
 * -----------------------------------------------------------------------
 * t_interval		W	-	R	R	-	R
 * t_repeat		W	-	R	-	-	-
 * t_left		W	-	W	-	-	RW
 * t_callback		W	-	-	-	-	R
 * t_data		W	-	-	-	-	R
 * t_expire		W	-	-	-	-	-
 * t_tid		W	-	-	-	W	R
 * t_stopping		W	-	-	W	-	R
 * t_stop_sem		W	W	-	W	-	W
 * t_ptimer		W	-	R	R	RW	R
 * t_signo		W	-	-	-	W	R
 *
 */

#ifndef C2_TIMER_DEBUG
#define C2_TIMER_DEBUG 1
#endif

/**
 * Hard timer implementation will use real-time signals from
 * TIMER_SIGNO_MIN to TIMER_SIGNO_MAX inclusive.
 */
#define TIMER_SIGNO_MIN		SIGRTMIN
#define TIMER_SIGNO_MAX		SIGRTMAX

/**
 * Function enum for timer_state_change() checks.
 */
enum timer_func {
	TIMER_INIT = 0,
	TIMER_FINI,
	TIMER_START,
	TIMER_STOP,
	TIMER_ATTACH,
	TIMER_FUNC_NR
};

/**
 * Item of threads ID list in locality.
 */
struct timer_tid {
	pid_t		tt_tid;
	struct c2_tlink tt_linkage;
	uint64_t	tt_magic;
};

/**
 * Signal number for every new hard timer is chosen in a round-robin fashion.
 * This variable contains signal number for next timer.
 */
static int signo_rr;
/* magic interval for c2_timer_stop() */
static c2_time_t magic_interval;
static c2_time_t zero_time;

/**
 * Typed list of timer_tid structures.
 */
C2_TL_DESCR_DEFINE(tid, "thread IDs", static, struct timer_tid,
		tt_linkage, tt_magic,
		0x696c444954726d74,	/** ASCII "tmrTIDli" - timer thread ID list item */
		0x686c444954726d74);	/** ASCII "tmrTIDlh" - timer thread ID list head */
C2_TL_DEFINE(tid, static, struct timer_tid);

/**
   Empty function for SIGUSR1 sighandler for soft timer scheduling thread.
   Using for wake up thread.
*/
static void nothing(int unused)
{
}

/**
   gettid(2) implementation.
   Thread-safe, async-signal-safe.
 */
static pid_t gettid() {

	return syscall(SYS_gettid);
}

static int signo_rr_get()
{
	int signo = signo_rr;

	signo_rr = signo_rr == TIMER_SIGNO_MAX ? TIMER_SIGNO_MIN : signo_rr + 1;
	return signo;
}

void c2_timer_locality_init(struct c2_timer_locality *loc)
{
	C2_PRE(loc != NULL);

	c2_mutex_init(&loc->tlo_lock);
	tid_tlist_init(&loc->tlo_tids);
	loc->tlo_rrtid = NULL;
	loc->tlo_signo = signo_rr_get();
}
C2_EXPORTED(c2_timer_locality_init);

void c2_timer_locality_fini(struct c2_timer_locality *loc)
{
	C2_PRE(loc != NULL);

	c2_mutex_fini(&loc->tlo_lock);
	tid_tlist_fini(&loc->tlo_tids);
}
C2_EXPORTED(c2_timer_locality_fini);

static struct timer_tid *locality_tid_find(struct c2_timer_locality *loc,
		pid_t tid)
{
	struct timer_tid *tt = NULL;

	c2_mutex_lock(&loc->tlo_lock);
	c2_tlist_for(&tid_tl, &loc->tlo_tids, tt) {
		if (tt->tt_tid == tid)
			break;
	} c2_tlist_endfor;
	c2_mutex_unlock(&loc->tlo_lock);

	return tt;
}

int c2_timer_thread_attach(struct c2_timer_locality *loc)
{
	pid_t tid;
	struct timer_tid *tt;

	C2_PRE(loc != NULL);

	tid = gettid();
	C2_ASSERT(locality_tid_find(loc, tid) == NULL);

	C2_ALLOC_PTR(tt);
	if (tt == NULL)
		return -ENOMEM;

	tt->tt_tid = tid;
	tid_tlink_init(tt);

	c2_mutex_lock(&loc->tlo_lock);
	tid_tlist_add(&loc->tlo_tids, tt);
	c2_mutex_unlock(&loc->tlo_lock);

	return 0;
}
C2_EXPORTED(c2_timer_thread_attach);

void c2_timer_thread_detach(struct c2_timer_locality *loc)
{
	pid_t tid;
	struct timer_tid *tt;

	C2_PRE(loc != NULL);

	tid = gettid();
	tt = locality_tid_find(loc, tid);
	C2_ASSERT(tt != NULL);

	c2_mutex_lock(&loc->tlo_lock);
	if (loc->tlo_rrtid == tt)
		loc->tlo_rrtid = NULL;
	tid_tlist_del(tt);
	c2_mutex_unlock(&loc->tlo_lock);

	tid_tlink_fini(tt);
	c2_free(tt);
}
C2_EXPORTED(c2_timer_thread_detach);

/**
   Init POSIX timer, write it to timer->t_ptimer.
   Timer notification is signal timer->t_signo to
   thread timer->t_tid.
 */
static int timer_posix_init(struct c2_timer *timer)
{
	struct sigevent se;

	se.sigev_notify = SIGEV_THREAD_ID;
	se.sigev_signo = timer->t_signo;
	se._sigev_un._tid = timer->t_tid;
	se.sigev_value.sival_ptr = timer;
	return timer_create(CLOCK_REALTIME, &se, &timer->t_ptimer);
}

/**
   Delete POSIX timer.
 */
static int timer_posix_fini(struct c2_timer *timer)
{
	return timer_delete(timer->t_ptimer);
}

/**
   Run timer_settime() with given expire time (absolute) and interval.
 */
static int timer_posix_set(struct c2_timer *timer,
		c2_time_t expire, c2_time_t interval)
{
	struct itimerspec ts;

	ts.it_interval.tv_sec = c2_time_seconds(interval);
	ts.it_interval.tv_nsec = c2_time_nanoseconds(interval);
	ts.it_value.tv_sec = c2_time_seconds(expire);
	ts.it_value.tv_nsec = c2_time_nanoseconds(expire);
	return timer_settime(timer->t_ptimer, TIMER_ABSTIME, &ts, NULL);
}

/**
   Get POSIX timer interval.
   Returns zero_time for disarmed timer.
 */
static void timer_posix_get(struct c2_timer *timer,
		c2_time_t *interval)
{
	struct itimerspec it;
	int rc;

	rc = timer_gettime(timer->t_ptimer, &it);
	/*
	 * timer_gettime() can only fail if &it isn't valid pointer or
	 * timer->t_ptimer isn't valid timer ID.
	 */
	C2_ASSERT(rc == 0);
	c2_time_set(interval, it.it_interval.tv_sec, it.it_interval.tv_nsec);
}

/**
   Set up signal handler sighandler for given signo.
 */
static int timer_sigaction(int signo,
		void (*sighandler)(int, siginfo_t*, void*))
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
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
	c2_time_t interval;
	c2_time_t expire;

	C2_PRE(si != NULL && si->si_value.sival_ptr != 0);
	C2_PRE(si->si_code == SI_TIMER);
	C2_PRE(signo >= TIMER_SIGNO_MIN && signo <= TIMER_SIGNO_MAX);

	timer = si->si_value.sival_ptr;
	C2_ASSERT(signo == timer->t_signo);
#ifdef C2_TIMER_DEBUG
	C2_ASSERT(timer->t_tid == gettid());
#endif

	if (timer->t_stopping) {
		/*
		 * When c2_timer is stopping, signal can be delivered for
		 * expiration (and POSIX timer will be currently disarmed)
		 * or for timer_settime() in timer_hard_stop() with
		 * magic_interval as interval. In this case timer_hard_stop()
		 * is locked on t_stop_sem semaphore.
		 */
		timer_posix_get(timer, &interval);
		if (interval == magic_interval) {
			timer_posix_set(timer, zero_time, zero_time);
			c2_semaphore_up(&timer->t_stop_sem);
		} else if (interval != zero_time) {
			C2_IMPOSSIBLE("impossible POSIX timer interval");
		}
	} else if (timer->t_left > 0) {
		/*
		 * POSIX timer is disabled.
		 * Execute callback and arm it if needed.
		 */
		timer->t_callback(timer->t_data);
		if (--timer->t_left > 0) {
			expire = c2_time_add(c2_time_now(), timer->t_interval);
			timer_posix_set(timer, expire, zero_time);
		}
	} else {
		C2_IMPOSSIBLE("impossible signal for c2_timer");
	}
}

/**
   This function called on every c2_timer_init/fini/start/stop/attach.
   It checks the possibility of transition from the current state
   with a given function and if possible, changes timer state to a new state
   or executes C2_ASSERT() otherwise.
 */
static void timer_state_change(struct c2_timer *timer, enum timer_func func)
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
				[TIMER_STOP]   = TIMER_INITED,
				[TIMER_ATTACH] = TIMER_INVALID
			}
		};

	new_state = func == TIMER_INIT ? TIMER_INITED :
		transition[timer->t_state][func];

	C2_ASSERT(new_state != TIMER_INVALID);
	timer->t_state = new_state;
}

/**
   Create POSIX timer for given c2_timer.
 */
static int timer_hard_init(struct c2_timer *timer)
{
	int rc;

	timer_state_change(timer, TIMER_INIT);
	timer->t_stopping = false;
	timer->t_tid = gettid();
	timer->t_signo = signo_rr_get();
	rc = timer_posix_init(timer);
	if (rc != 0)
		return rc;
	rc = c2_semaphore_init(&timer->t_stop_sem, 0);
	if (rc != 0) {
		timer_posix_fini(timer);
		return rc;
	}
	return 0;
}

/**
   Delete POSIX timer for given c2_timer.
 */
static int timer_hard_fini(struct c2_timer *timer)
{
	timer_state_change(timer, TIMER_FINI);
	c2_semaphore_fini(&timer->t_stop_sem);
	return timer_posix_fini(timer);
}

/**
   Start one-shot POSIX timer for given c2_timer.
   After every executed callback POSIX timer will be set again
   to one-shot timer if necessary.
 */
static int timer_hard_start(struct c2_timer *timer)
{
	c2_time_t expire;

	timer_state_change(timer, TIMER_START);
	if (timer->t_repeat != 0) {
		timer->t_left = timer->t_repeat;
		expire = c2_time_add(c2_time_now(), timer->t_interval);
		return timer_posix_set(timer, expire, zero_time);
	}
	return 0;
}

/**
   Stop POSIX timer.
 */
static int timer_hard_stop(struct c2_timer *timer)
{
	int rc;

	timer_state_change(timer, TIMER_STOP);
	timer->t_stopping = true;
	rc = timer_posix_set(timer, c2_time_now(), magic_interval);
	if (rc != 0)
		return rc;
	/* wait until internal POSIX timer is disarmed */
	c2_semaphore_down(&timer->t_stop_sem);
	timer->t_stopping = false;
	return 0;
}

void c2_timer_attach(struct c2_timer *timer, struct c2_timer_locality *loc)
{
	struct timer_tid *tt;

	C2_PRE(loc != NULL);
	C2_PRE(timer != NULL);
	C2_PRE(timer->t_type == C2_TIMER_HARD);

	timer_state_change(timer, TIMER_ATTACH);

	c2_mutex_lock(&loc->tlo_lock);
	C2_ASSERT(!tid_tlist_is_empty(&loc->tlo_tids));
	if (loc->tlo_rrtid == NULL)
		loc->tlo_rrtid = tid_tlist_head(&loc->tlo_tids);
	tt = loc->tlo_rrtid;
	loc->tlo_rrtid = tid_tlist_next(&loc->tlo_tids, tt);
	c2_mutex_unlock(&loc->tlo_lock);

	timer->t_tid = tt->tt_tid;
	timer->t_signo = loc->tlo_signo;

	timer_posix_fini(timer);
	timer_posix_init(timer);
}
C2_EXPORTED(c2_timer_attach);

/**
   Soft timer working thread
 */
static void c2_timer_working_thread(struct c2_timer *timer)
{
	c2_time_t next;
	c2_time_t now;
	c2_time_t rem;
	int rc;

	c2_time_set(&rem, 0, 0);
	/* capture this signal. It is used to wake this thread */
	signal(SIGUSR1, nothing);

	while (timer->t_left > 0) {
		now = c2_time_now();
		if (c2_time_after(now, timer->t_expire))
			timer->t_expire = c2_time_add(now, timer->t_interval);

		next = c2_time_sub(timer->t_expire, now);
		while (timer->t_left > 0 &&
				(rc = c2_nanosleep(next, &rem)) != 0) {
			next = rem;
		}
		if (timer->t_left == 0)
			break;
		timer->t_expire = c2_time_add(timer->t_expire,
				timer->t_interval);
		timer->t_callback(timer->t_data);
		--timer->t_left;
		if (timer->t_left == 0 || timer->t_left == ~0)
			break;
	}
}

/**
   Init the timer data structure.
 */
int c2_timer_init(struct c2_timer *timer, enum c2_timer_type type,
		  c2_time_t interval, uint64_t repeat,
		  c2_timer_callback_t callback, unsigned long data)
{
	C2_PRE(callback != NULL);
	C2_PRE(type == C2_TIMER_SOFT || type == C2_TIMER_HARD);
	C2_PRE(timer != NULL);

	C2_SET0(timer);
	timer->t_type     = type;
	timer->t_interval = interval;
	timer->t_repeat   = repeat;
	timer->t_left     = 0;
	timer->t_callback = callback;
	timer->t_data     = data;
	c2_time_set(&timer->t_expire, 0, 0);

	if (type == C2_TIMER_HARD)
		return timer_hard_init(timer);
	return 0;
}
C2_EXPORTED(c2_timer_init);

/**
   Start a timer.
 */
int c2_timer_start(struct c2_timer *timer)
{
	int rc;

	C2_PRE(timer != NULL);

	timer->t_expire = c2_time_add(c2_time_now(), timer->t_interval);
	timer->t_left = timer->t_repeat;

	if (timer->t_type == C2_TIMER_HARD) {
		rc = timer_hard_start(timer);
	} else {
		rc = C2_THREAD_INIT(&timer->t_thread, struct c2_timer*, NULL,
				    &c2_timer_working_thread,
				    timer, "c2_timer_worker");
	}

	return rc;
}
C2_EXPORTED(c2_timer_start);

/**
   Stop a timer.
 */
int c2_timer_stop(struct c2_timer *timer)
{
	C2_PRE(timer != NULL);

	if (timer->t_type == C2_TIMER_HARD) {
		timer_hard_stop(timer);
	} else {
		timer->t_left = 0;
		c2_time_set(&timer->t_expire, 0, 0);

		if (timer->t_thread.t_func != NULL) {
			c2_thread_signal(&timer->t_thread, SIGUSR1);
			c2_thread_join(&timer->t_thread);
			c2_thread_fini(&timer->t_thread);
		}
	}

	return 0;
}
C2_EXPORTED(c2_timer_stop);

/**
   Destroy the timer.
 */
void c2_timer_fini(struct c2_timer *timer)
{
	C2_PRE(timer != NULL);

	if (timer->t_type == C2_TIMER_HARD)
		timer_hard_fini(timer);

	C2_SET0(timer);
}
C2_EXPORTED(c2_timer_fini);

/**
   Init data structures for hard timer
 */
int c2_timers_init()
{
	int i;
	int rc;

	C2_ASSERT(TIMER_SIGNO_MIN <= TIMER_SIGNO_MAX);

	signo_rr = TIMER_SIGNO_MIN;
	for (i = TIMER_SIGNO_MIN; i <= TIMER_SIGNO_MAX; ++i) {
		rc = timer_sigaction(i, timer_sighandler);
		/*
		 * sigaction() can only fail if there is
		 * a logic error in this implementation
		 */
		C2_ASSERT(rc == 0);
	}
	c2_time_set(&magic_interval, 60 * 60 * 24, 606024);
	c2_time_set(&zero_time, 0, 0);
	return 0;
}
C2_EXPORTED(c2_timers_init);

/**
   fini() all remaining hard timer data structures
 */
void c2_timers_fini()
{
}
C2_EXPORTED(c2_timers_fini);

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
