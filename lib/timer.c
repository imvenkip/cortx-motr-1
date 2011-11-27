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

#ifndef C2_TIMER_DEBUG
#define C2_TIMER_DEBUG 1
#endif

#define SIGTIMERSCHED SIGRTMIN
#define SIGTIMERCALL (SIGRTMIN + 1)

enum {
	PIPE_BUF_SIZE = 4096,
};

enum timer_state_new {
	TS_START,
	TS_STOP,
	TS_FINI
};

struct c2_timer_info {
	struct c2_tlink	    ti_linkage;
	uint64_t	    ti_magic;
	c2_time_t	    ti_expire;
	c2_time_t	    ti_interval;
	int		    ti_repeat;
	int		    ti_left;
	pid_t		    ti_tid;
	c2_timer_callback_t ti_callback;
	long		    ti_data;
};

struct timer_pipe {
	int		   tp_pipefd[2];
	struct c2_atomic64 tp_size;
};

struct timer_tid {
	pid_t		tt_tid;
	struct c2_tlink tt_linkage;
	uint64_t	tt_magic;
};

struct timer_state {
	struct c2_tlink	      ts_linkage;
	uint64_t	      ts_magic;
	struct c2_timer_info *ts_tinfo;
	enum timer_state_new  ts_state;
};

static struct c2_thread scheduler;
static struct c2_tl timer_pqueue;
static struct c2_tl state_queue;
static struct c2_mutex state_lock;
static struct c2_semaphore state_sem;
static struct c2_semaphore callback_sem;
static volatile struct c2_timer_info *callback_tinfo;
static struct c2_semaphore sched_init_lock;
static bool finish_scheduler;
static int sched_errno;

C2_TL_DESCR_DEFINE(tid, "thread IDs", static, struct timer_tid,
		tt_linkage, tt_magic,
		0x48e42721b3738bfd, 0xc2fe5365aafe4064);
C2_TL_DEFINE(tid, static, struct timer_tid);

C2_TL_DESCR_DEFINE(ti, "c2_timer_info priority queue", static,
		struct c2_timer_info, ti_linkage, ti_magic,
		0x8dd02707b3f69ca4, 0xf112a93259bffcb0);
C2_TL_DEFINE(ti, static, struct c2_timer_info);

C2_TL_DESCR_DEFINE(ts, "timer_state queue", static,
		struct timer_state, ts_linkage, ts_magic,
		0xabb9e83856d504bf, 0x75706d63a355633b);
C2_TL_DEFINE(ts, static, struct timer_state);

/**
	Empty function for SIGUSR1 sighandler for soft timer scheduling thread.
	Using for wake up thread.
*/
static void nothing(int unused)
{
}

/**
   gettid(2) implementation.
   Thread-safe, async-sighal-safe.
 */
pid_t gettid() {

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
	struct timer_tid *tt;

	C2_PRE(loc != NULL);

	C2_ASSERT(!tid_tlist_is_empty(&loc->tlo_tids));
	c2_mutex_fini(&loc->tlo_lock);
	while ((tt = tid_tlist_head(&loc->tlo_tids)) != NULL) {
		tid_tlist_del(tt);
		tid_tlink_fini(tt);
	}
	tid_tlist_fini(&loc->tlo_tids);
}

static bool locality_tid_contains(struct c2_timer_locality *loc, pid_t tid)
{
	struct timer_tid *tt;
	bool found = false;

	c2_mutex_lock(&loc->tlo_lock);
	c2_tlist_for(&tid_tl, &loc->tlo_tids, tt) {
		if (tt->tt_tid == tid) {
			found = true;
			break;
		}
	} c2_tlist_endfor;
	c2_mutex_unlock(&loc->tlo_lock);
	return found;
}

int c2_timer_thread_attach(struct c2_timer_locality *loc)
{
	pid_t tid;
	struct timer_tid *tt;

	C2_PRE(loc != NULL);

	tid = gettid();
	C2_ASSERT(!locality_tid_contains(loc, tid));

	C2_ALLOC_PTR(tt);
	if (tt == NULL)
		return -ENOMEM;

	tt->tt_tid = tid;
	tid_tlink_init(tt);
	c2_mutex_lock(&loc->tlo_lock);
	tid_tlist_add(&loc->tlo_tids, tt);
	loc->tlo_rrtid = NULL;
	c2_mutex_unlock(&loc->tlo_lock);
	return 0;
}

void c2_timer_attach(struct c2_timer *timer, struct c2_timer_locality *loc)
{
	pid_t tid;
	struct timer_tid *tt;

	C2_PRE(loc != NULL);
	C2_PRE(timer != NULL);
	C2_PRE(timer->t_info != NULL);

	tid = gettid();
	c2_mutex_lock(&loc->tlo_lock);
	C2_ASSERT(!tid_tlist_is_empty(&loc->tlo_tids));
	if (loc->tlo_rrtid == NULL)
		loc->tlo_rrtid = tid_tlist_head(&loc->tlo_tids);
	tt = loc->tlo_rrtid;
	loc->tlo_rrtid = tid_tlist_next(&loc->tlo_tids, tt);
	c2_mutex_unlock(&loc->tlo_lock);
	timer->t_info->ti_tid = tt->tt_tid;
}

static struct c2_timer_info *timer_info_init(struct c2_timer *timer)
{
	struct c2_timer_info *tinfo;

	C2_ALLOC_PTR(tinfo);
	if (tinfo == NULL)
		return NULL;

	ti_tlink_init(tinfo);
	tinfo->ti_expire = c2_time_now();
	tinfo->ti_interval = timer->t_interval;
	tinfo->ti_left = 0;
	tinfo->ti_repeat = timer->t_repeat;
	tinfo->ti_tid = 0;
	tinfo->ti_callback = timer->t_callback;
	tinfo->ti_data = timer->t_data;
	return tinfo;
}

static void timer_info_fini(struct c2_timer_info *tinfo)
{
	ti_tlink_fini(tinfo);
	c2_free(tinfo);
}

static int timer_state_enqueue(struct c2_timer_info *tinfo,
		enum timer_state_new state)
{
	struct timer_state *ts;

	C2_PRE(tinfo != NULL);
	C2_ALLOC_PTR(ts);
	if (ts == NULL)
		return -ENOMEM;

	ts_tlink_init(ts);
	ts->ts_tinfo = tinfo;
	ts->ts_state = state;

	c2_mutex_lock(&state_lock);
	ts_tlist_add_tail(&state_queue, ts);
	c2_mutex_unlock(&state_lock);

	return 0;
}

static struct c2_timer_info *timer_state_dequeue(enum timer_state_new *state)
{
	struct c2_timer_info *tinfo;
	struct timer_state   *ts;

	c2_mutex_lock(&state_lock);
	if (ts_tlist_is_empty(&state_queue)) {
		tinfo = NULL;
	} else {
		ts = ts_tlist_head(&state_queue);
		tinfo = ts->ts_tinfo;
		*state = ts->ts_state;

		ts_tlist_del(ts);
		ts_tlink_fini(ts);
		c2_free(ts);
	}
	c2_mutex_unlock(&state_lock);

	return tinfo;
}

static void timer_pqueue_insert(struct c2_timer_info *tinfo)
{
	if (tinfo->ti_left == 0)
		return;
	ti_tlist_add(&timer_pqueue, tinfo);
}

static void timer_pqueue_remove(struct c2_timer_info *tinfo)
{
	if (ti_tlink_is_in(tinfo))
		ti_tlist_del(tinfo);
}

static struct c2_timer_info *timer_pqueue_min()
{
	struct c2_timer_info *min;
	struct c2_timer_info *tinfo;

	if (ti_tlist_is_empty(&timer_pqueue))
		return NULL;
	min = ti_tlist_head(&timer_pqueue);
	c2_tlist_for(&ti_tl, &timer_pqueue, tinfo) {
		if (!c2_time_after_eq(tinfo->ti_expire, min->ti_expire))
			min = tinfo;
	} c2_tlist_endfor;
	return min;
}

static timer_t timer_posix_init(int signo, pid_t tid)
{
	struct sigevent se;
	timer_t timer;

	se.sigev_notify = SIGEV_THREAD_ID;
	se.sigev_signo = signo;
	se._sigev_un._tid = tid;
	C2_ASSERT(timer_create(CLOCK_REALTIME, &se, &timer) == 0);
	return timer;
}

static void timer_posix_fini(timer_t timer)
{
	C2_ASSERT(timer_delete(timer) == 0);
}

static void timer_posix_set(timer_t timer, c2_time_t expire)
{
	struct itimerspec ts;

	ts.it_interval.tv_sec = 0;
	ts.it_interval.tv_nsec = 0;
	ts.it_value.tv_sec = c2_time_seconds(expire);
	ts.it_value.tv_nsec = c2_time_nanoseconds(expire);
	C2_ASSERT(timer_settime(timer, TIMER_ABSTIME, &ts, NULL) == 0);
}

static void callback_sighandler(int unused)
{
#ifdef C2_TIMER_DEBUG
	C2_ASSERT(callback_tinfo->ti_tid == gettid());
#endif
	callback_tinfo->ti_callback(callback_tinfo->ti_data);
	callback_tinfo = NULL;
	c2_semaphore_up(&callback_sem);
}

static void callback_execute(struct c2_timer_info *tinfo)
{
	timer_t ptimer;

	C2_PRE(tinfo != NULL);

	callback_tinfo = tinfo;
	ptimer = timer_posix_init(SIGTIMERCALL, tinfo->ti_tid);
	timer_posix_set(ptimer, c2_time_now());
	c2_semaphore_down(&callback_sem);
	timer_posix_fini(ptimer);
}

static void timer_reschedule(struct c2_timer_info *tinfo)
{
	C2_PRE(tinfo != NULL);

	if (--tinfo->ti_left == 0)
		timer_pqueue_remove(tinfo);
	else
		tinfo->ti_expire = c2_time_add(c2_time_now(),
				tinfo->ti_interval);
}

static struct c2_timer_info *timer_expired_execute()
{
	struct c2_timer_info *min;

	while (1) {
		min = timer_pqueue_min();
		if (min == NULL || c2_time_after(min->ti_expire, c2_time_now()))
			break;
		callback_execute(min);
		timer_reschedule(min);
	}
	return min;
}

static void timer_scheduler_sighandler(int unused)
{
	c2_semaphore_up(&state_sem);
}

static int timer_sigaction(int signo, void (*handler)(int))
{
	struct sigaction sa;

	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (sigaction(signo, &sa, NULL) != 0)
		return errno;
	return 0;
}

static void timer_state_process()
{
	struct c2_timer_info *tinfo;
	enum timer_state_new state;

	while ((tinfo = timer_state_dequeue(&state)) != NULL) {
		switch (state) {
		case TS_FINI:
			timer_pqueue_remove(tinfo);
			timer_info_fini(tinfo);
			break;
		case TS_START:
			tinfo->ti_expire = c2_time_add(c2_time_now(),
					tinfo->ti_interval);
			tinfo->ti_left = tinfo->ti_repeat;
			timer_pqueue_insert(tinfo);
			break;
		case TS_STOP:
			timer_pqueue_remove(tinfo);
			break;
		default:
			C2_IMPOSSIBLE("invalid state");
		}
	}
}

static void timer_scheduler(int unused)
{
	struct c2_timer_info *min;
	timer_t ptimer;

	sched_errno = timer_sigaction(SIGTIMERSCHED,
			timer_scheduler_sighandler);
	if (sched_errno == 0)
		sched_errno = timer_sigaction(SIGTIMERCALL,
				callback_sighandler);

	c2_semaphore_up(&sched_init_lock);
	if (sched_errno != 0)
		return;

	ptimer = timer_posix_init(SIGTIMERSCHED, gettid());
	while (1) {
		c2_semaphore_down(&state_sem);
		timer_state_process();
		if (finish_scheduler) {
			timer_state_process();
			break;
		}
		min = timer_expired_execute();
		if (min == NULL)
			continue;
		timer_posix_set(ptimer, min->ti_expire);
	}
	timer_posix_fini(ptimer);
}

static int timer_hard_init(struct c2_timer *timer)
{
	C2_PRE(timer != NULL);

	timer->t_info = timer_info_init(timer);

	return timer->t_info == NULL;
}

static int timer_state_deliver(struct c2_timer *timer,
		enum timer_state_new state)
{
	int rc;

	C2_PRE(timer != NULL);
	C2_PRE(timer->t_info != NULL);

	rc = timer_state_enqueue(timer->t_info, state);
	if (rc == 0)
		c2_semaphore_up(&state_sem);
	return rc;
}

static int timer_hard_fini(struct c2_timer *timer)
{
	return timer_state_deliver(timer, TS_FINI);
}

static int timer_hard_start(struct c2_timer *timer)
{
	C2_PRE(timer != NULL);
	C2_PRE(timer->t_info != NULL);
	C2_PRE(timer->t_info->ti_tid != 0);

	return timer_state_deliver(timer, TS_START);
}

static int timer_hard_stop(struct c2_timer *timer)
{
	return timer_state_deliver(timer, TS_STOP);
}

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
		/** @todo
		 * race condition problem here
		 * if between (timer->t_left == 0) and (--timer->t_left == 0)
		 * executed timer->t_left = 0; from c2_timer_stop()
		 * then we have very big loop here and lock in c2_timer_stop()
		*/
		if (timer->t_left == 0 || --timer->t_left == 0)
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
	if (timer->t_type == C2_TIMER_HARD)
		timer_hard_fini(timer);

	C2_SET0(timer);
}
C2_EXPORTED(c2_timer_fini);

static void timer_data_init()
{
	ti_tlist_init(&timer_pqueue);
	ts_tlist_init(&state_queue);
	c2_mutex_init(&state_lock);
	C2_ASSERT(c2_semaphore_init(&state_sem, 0) == 0);
	C2_ASSERT(c2_semaphore_init(&callback_sem, 0) == 0);
	finish_scheduler = false;
	callback_tinfo = NULL;
}

static void timer_data_fini()
{
	ti_tlist_fini(&timer_pqueue);
	ts_tlist_fini(&state_queue);
	c2_mutex_fini(&state_lock);
	C2_ASSERT(callback_tinfo == NULL);
	c2_semaphore_fini(&state_sem);
	c2_semaphore_fini(&callback_sem);
}

/**
   Init data structures for hard timer
 */
int c2_timers_init()
{
	int rc;

	timer_data_init();
	c2_semaphore_init(&sched_init_lock, 0);

	rc = C2_THREAD_INIT(&scheduler, int, NULL,
			&timer_scheduler, 0,
			"hard timer scheduler");

	if (rc != 0) {
		timer_data_fini();
		return rc;
	}

	c2_semaphore_down(&sched_init_lock);
	c2_semaphore_fini(&sched_init_lock);

	return sched_errno;
}
C2_EXPORTED(c2_timers_init);

/**
   fini() all remaining hard timer data structures
 */
void c2_timers_fini()
{
	finish_scheduler = true;
	c2_semaphore_up(&state_sem);
	c2_thread_join(&scheduler);
	c2_thread_fini(&scheduler);

	timer_data_fini();
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
