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

// FIXME hack, but without it timer_create(2) isn't declarated.
// in Makefile should be -iquote instead of -I
#include </usr/include/time.h>	  /* timer_create */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/mutex.h"  /* c2_mutex */
#include "lib/cond.h"
#include "lib/thread.h" /* c2_thread */
#include "lib/assert.h" /* C2_ASSERT */
#include "lib/atomic.h" /* c2_atomic64 */
#include "lib/memory.h" /* c2_alloc */
#include "lib/queue.h"  /* c2_queue */
#include "lib/errno.h"	/* ENOMEM */
#include "lib/rbtree.h" /* c2_rbtree */
#include "lib/cdefs.h"  /* container_of */
#include "lib/rwlock.h" /* c2_rwlock */
#include "lib/arith.h"	/* min_check */
#include "lib/aqueue.h" /* c2_aqueue */
#include "lib/time.h"	/* c2_time_t */

#include "lib/timer.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>	  /* syscall */
#include <signal.h>	  /* timer_create */
#include <sys/syscall.h>  /* syscall */
#include <limits.h>	  /* INT_MAX */
#include <linux/limits.h> /* RTSIG_MAX */

/**
   @addtogroup timer

   Implementation of c2_timer.

   In userspace soft timer implementation, there is a timer thread running,
   which checks the expire time and trigger timer callback if needed.
   There is one timer thread for each timer.

   TODO what kind of error should return when out of memory ?
   TODO check includes
   TODO 80 char per line
   TODO rename c2_timer_sighandler
   TODO static functions if needed
   TODO remove unused fields
   hard timer
	c2_timer
		c2_timer_info *t_info
	c2_timer_info
		c2_rbtree_link ti_linkage
		c2_aqueue_link ti_aqlink
		c2_time_t ti_expire
		c2_time_t ti_interval
		int ti_left
		c2_timer_callback_t ti_callback
		unsigned long ti_data
		c2_timer_locality *ti_loc
		pid_t ti_tid
		c2_timer_sighandler *ti_sighandler
	c2_timer_locality
		int tlo_signo
		c2_timer_sighandler *tlo_sighandler
		c2_mutex tlo_lock	// mutex for tlo_tids
		rbtree tlo_tids
	c2_timer_sighandler
		int tsh_signo
		rbtree tsh_pqueue	// timers priority queue
		atomic64 tsh_callback_pending
		atomic64 tsh_callback_executing
		atomic64 tsh_processing
		pid_t tsh_callback_tid
		c2_timer_callback_t tsh_callback
		unsigned long tsh_data
		atomic_queue tsh_state_queue
		atomic_queue tsh_tinfo_dealloc
		atomic_queue tsh_tstate_dealloc
		c2_atomic64 tsh_timers_count
		c2_time_t tsh_expire	// abs time
		c2_time_t tsh_expire_tid
		timer_t tsh_ptimer
		pid_t tsh_ptimer_thread
		c2_time_t tsh_ptimer_expire	// abs time
		atomic64 tsh_ptimer_processing
	c2_timer_state
		c2_aqueue_link ts_linkage
		c2_timer_info *ts_tinfo
		TIMER_STATE ts_state
	c2_timer_tid
		c2_rbtree_link tt_linkage
		pid_t tt_pid

	с2_timer_locality_init()
		find signo with minimal timers count
		assign this signo to locality
		set up link to sighandler
	c2_timer_locality_fini()
	c2_timer_locality_max()
	c2_timer_locality_count()

	c2_timer_thread_attach()
		blocking
		add tid to locality

	c2_timer_init/fini/start/stop() in the start
		deallocate all from deallocation queue
	c2_timer_fini/start/stop() in the end
		execute sighandler

	c2_timer_init()
		alloc c2_timer_info
	c2_timer_fini()
		queue state FINI
	c2_timer_attach()
		if current tid is in locality
			set ti_tid = gettid()
		else
			add to first thread in locality
	c2_timer_start()
		set interval, left, expiration of c2_timer_info from c2_timer
		queue state START
	c2_timer_stop()
		queue state STOP

   sighandler
	if atomic_inc tsh_processsing != 1
		return;
	do
		clear min_aqueue
		for every new state from queue
		FINI	add timer_info to deallocation queue
		START	add timer_info to timers priority queue for current location
		STOP	remove timer_info from timers priority queue
		for every expired timer where tid matches gettid()
			execute it
			reschedule it
		tinfo = min in pqueue
		add tinfo to min_aqueue
	while atomic_dec tsh_processing != 0
	jump to last in min_aqueue
	
*/

struct c2_timer_sighandler {
	int tsh_signo;
	struct c2_rbtree tsh_pqueue;
	struct c2_atomic64 tsh_callback_pending;
	struct c2_atomic64 tsh_callback_executing;
	struct c2_atomic64 tsh_processing;
	pid_t tsh_callback_tid;
	c2_timer_callback_t tsh_callback;
	unsigned long tsh_data;
	struct c2_aqueue tsh_state_queue;
	struct c2_aqueue ths_tinfo_dealloc;
	struct c2_aqueue tsh_tstate_dealloc;
	struct c2_atomic64 tsh_timers_count;
	c2_time_t tsh_expire;
	pid_t tsh_expire_tid;
	struct c2_atomic64 tsh_expire_set;
	timer_t tsh_ptimer;
	pid_t tsh_ptimer_thread;
	c2_time_t tsh_ptimer_expire;
	struct c2_atomic64 tsh_ptimer_processing;
	struct c2_aqueue tsh_aqueue_min;
};

struct c2_timer_info {
	struct c2_rbtree_link ti_linkage;
	struct c2_aqueue_link ti_aqlink;
	struct c2_aqueue_link ti_aqlink_min;
	c2_time_t ti_expire;
	c2_time_t ti_interval;
	int ti_left;
	c2_timer_callback_t ti_callback;
	unsigned long ti_data;
	struct c2_timer_locality *ti_loc;
	pid_t ti_tid;	// destination thread ID
	struct c2_timer_sighandler *ti_sighandler;
};

enum TIMER_STATE {
	TIMER_STATE_START = 1,
	TIMER_STATE_STOP,
	TIMER_STATE_FINI
};

struct c2_timer_state {
	struct c2_aqueue_link ts_linkage;
	struct c2_timer_info *ts_tinfo;
	enum TIMER_STATE ts_state;
};

struct c2_timer_tid {
	struct c2_rbtree_link tt_linkage;
	pid_t tt_pid;
};

static struct c2_timer_sighandler timer_sighandlers[RTSIG_MAX];
static struct c2_atomic64 timer_locality_count;
static c2_time_t timer_1ns;
static c2_time_t timer_infinity;
static struct c2_atomic64 timer_deallocating;

/**
	Empty function for SIGUSR1 sighandler for soft timer scheduling thread.
	Using for wake up thread.
*/
void nothing(int unused)
{
}

/**
   gettid(2) implementation.
   Thread-safe, async-sighal-safe.
 */
pid_t gettid() {

	return syscall(SYS_gettid);
}

/**
   Thread-safe, not async-signal-safe.
 */
void timer_state_enqueue(struct c2_timer_info *tinfo, enum TIMER_STATE state)
{
	struct c2_timer_state *tstate = c2_alloc(sizeof *tstate);

	C2_ASSERT(tstate != NULL);
	c2_aqueue_link_init(&tstate->ts_linkage);
	tstate->ts_tinfo = tinfo;
	tstate->ts_state = state;
	c2_aqueue_put(&tinfo->ti_sighandler->tsh_state_queue, &tstate->ts_linkage);
}

/**
   Thread-safe, async-signal-safe.
 */
struct c2_timer_info* timer_state_dequeue(struct c2_timer_sighandler *sighandler,
		enum TIMER_STATE *state)
{
	struct c2_aqueue_link *link;
	struct c2_timer_state *tstate;

	link = c2_aqueue_get(&sighandler->tsh_state_queue);
	if (link == NULL)
		return NULL;

	c2_aqueue_put(&sighandler->tsh_tstate_dealloc, link);
	tstate = container_of(link, struct c2_timer_state, ts_linkage);
	*state = tstate->ts_state;
	return tstate->ts_tinfo;

}

/**
   Comparator for red-black tree for timers priority queue
   Return
	-1 if a < b
	0  if a == b
	1  if a > b
*/
static int timer_expire_cmp(void *_a, void *_b)
{
	c2_time_t a = *(c2_time_t *) _a;
	c2_time_t b = *(c2_time_t *) _b;

	if (c2_time_after_eq(a, b) && c2_time_after_eq(b, a))
		return 0;
	else
		return c2_time_after(a, b) ? 1 : -1;
}

void timer_posix_init(struct c2_timer_sighandler *sighandler, pid_t tid)
{
	struct sigevent se;

	se.sigev_notify = SIGEV_THREAD_ID;
	se.sigev_signo = sighandler->tsh_signo;
	se._sigev_un._tid = tid;
	C2_ASSERT(timer_create(CLOCK_REALTIME, &se, &sighandler->tsh_ptimer) == 0);
	sighandler->tsh_ptimer_thread = tid;
}

void timer_posix_fini(struct c2_timer_sighandler *sighandler)
{
	C2_ASSERT(timer_delete(sighandler->tsh_ptimer) == 0);
	sighandler->tsh_ptimer_thread = 0;
}

static void timer_hard_sighandler(int signo);

void timer_sighandler_init(struct c2_timer_sighandler *sighandler, int signo)
{
	struct sigaction sa;
	C2_PRE(sighandler != NULL);

	sighandler->tsh_signo = signo;
	c2_rbtree_init(&sighandler->tsh_pqueue, timer_expire_cmp,
			offsetof(struct c2_timer_info, ti_expire) -
			offsetof(struct c2_timer_info, ti_linkage));
	c2_atomic64_set(&sighandler->tsh_callback_pending, 0);
	c2_atomic64_set(&sighandler->tsh_callback_executing, 0);
	c2_atomic64_set(&sighandler->tsh_processing, 0);
	sighandler->tsh_callback = NULL;
	sighandler->tsh_data = 0;
	c2_aqueue_init(&sighandler->tsh_state_queue);
	c2_aqueue_init(&sighandler->ths_tinfo_dealloc);
	c2_aqueue_init(&sighandler->tsh_tstate_dealloc);
	c2_atomic64_set(&sighandler->tsh_timers_count, 0);
	c2_time_set(&sighandler->tsh_expire, 0, 0);
	sighandler->tsh_expire_tid = 0;
	c2_atomic64_set(&sighandler->tsh_expire_set, 0);
	sighandler->tsh_ptimer = 0;
	sighandler->tsh_ptimer_thread = 0;
	c2_time_set(&sighandler->tsh_ptimer_expire, 0, 0);
	c2_atomic64_set(&sighandler->tsh_ptimer_processing, 0);
	c2_aqueue_init(&sighandler->tsh_aqueue_min);

	sa.sa_handler = timer_hard_sighandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NODEFER;
	C2_ASSERT(sigaction(signo, &sa, NULL) == 0);
	timer_posix_init(sighandler, gettid());
}

void timer_sighandler_fini(struct c2_timer_sighandler *sighandler)
{
	C2_PRE(sighandler != NULL);

	c2_aqueue_fini(&sighandler->tsh_aqueue_min);
	C2_ASSERT(c2_atomic64_get(&sighandler->tsh_ptimer_processing) == 0);
	C2_ASSERT(c2_atomic64_get(&sighandler->tsh_timers_count) == 0);
	c2_aqueue_fini(&sighandler->tsh_tstate_dealloc);
	c2_aqueue_fini(&sighandler->ths_tinfo_dealloc);
	c2_aqueue_fini(&sighandler->tsh_state_queue);
	C2_ASSERT(sighandler->tsh_callback == NULL);
	C2_ASSERT(c2_atomic64_get(&sighandler->tsh_processing) == 0);
	C2_ASSERT(c2_atomic64_get(&sighandler->tsh_callback_executing) == 0);
	C2_ASSERT(c2_atomic64_get(&sighandler->tsh_callback_pending) == 0);
	c2_rbtree_fini(&sighandler->tsh_pqueue);
	timer_posix_fini(sighandler);
}

size_t timer_sighandler_max()
{
	return min_check(_POSIX_TIMER_MAX,
			min_check(SIGRTMAX - SIGRTMIN + 1, RTSIG_MAX));
}

struct c2_timer_sighandler *timer_sighandler(int signo)
{
	C2_ASSERT(signo >= SIGRTMIN && signo < timer_sighandler_max() + SIGRTMIN);
	return &timer_sighandlers[signo - SIGRTMIN];
}

struct c2_timer_info *timer_info_init(struct c2_timer *timer)
{
	struct c2_timer_info *tinfo = c2_alloc(sizeof *tinfo);

	c2_rbtree_link_init(&tinfo->ti_linkage);
	c2_aqueue_link_init(&tinfo->ti_aqlink);
	c2_aqueue_link_init(&tinfo->ti_aqlink_min);
	c2_time_set(&tinfo->ti_expire, 0, 0);
	tinfo->ti_interval = timer->t_interval;
	tinfo->ti_left = timer->t_left;
	tinfo->ti_callback = timer->t_callback;
	tinfo->ti_data = timer->t_data;
	tinfo->ti_loc = NULL;
	tinfo->ti_tid = 0;
	tinfo->ti_sighandler = NULL;
	return tinfo;
}

void timer_info_fini(struct c2_timer_info *tinfo)
{
	if (tinfo->ti_sighandler != NULL)
		c2_atomic64_dec(&tinfo->ti_sighandler->tsh_timers_count);
	C2_ASSERT(tinfo->ti_left == 0);
	c2_aqueue_link_fini(&tinfo->ti_aqlink_min);
	c2_aqueue_link_fini(&tinfo->ti_aqlink);
	c2_rbtree_link_fini(&tinfo->ti_linkage);
	c2_free(tinfo);
}

/**
   Comparator for red-black tree for tlo_tids
*/
static int tids_cmp(void *_a, void *_b)
{
	pid_t a = *(pid_t *) _a;
	pid_t b = *(pid_t *) _b;

	return a == b ? 0 : a < b ? -1 : 1;
}

int c2_timer_locality_init(struct c2_timer_locality *loc)
{
	int i;
	int signo;
	int min_timers = INT_MAX;
	int timers;

	C2_PRE(loc != NULL);
	for (i = SIGRTMIN; i < SIGRTMIN + timer_sighandler_max(); ++i) {
		timers = c2_atomic64_get(&timer_sighandler(i)->tsh_timers_count);
		if (timers < min_timers) {
			signo = i;
			min_timers = timers;
		}
	}
	loc->tlo_signo = signo;
	loc->tlo_sighandler = timer_sighandler(signo);
	c2_mutex_init(&loc->tlo_lock);
	c2_rbtree_init(&loc->tlo_tids, tids_cmp,
			offsetof(struct c2_timer_tid, tt_pid) -
			offsetof(struct c2_timer_tid, tt_linkage));
	c2_atomic64_inc(&timer_locality_count);
	return 0;
}

void c2_timer_locality_fini(struct c2_timer_locality *loc)
{
	struct c2_rbtree_link *link;

	C2_PRE(loc != NULL);

	while ((link = c2_rbtree_min(&loc->tlo_tids)) != NULL)
		c2_rbtree_remove(&loc->tlo_tids, link);
	c2_rbtree_fini(&loc->tlo_tids);
	c2_mutex_fini(&loc->tlo_lock);
	c2_atomic64_dec(&timer_locality_count);
}

void c2_timer_thread_attach(struct c2_timer_locality *loc)
{
	struct c2_timer_tid *ttid;

	C2_PRE(loc != NULL);
	ttid = c2_alloc(sizeof *ttid);
	C2_ASSERT(ttid != NULL);

	c2_mutex_lock(&loc->tlo_lock);
	C2_ASSERT(c2_rbtree_insert(&loc->tlo_tids, &ttid->tt_linkage));
	c2_mutex_unlock(&loc->tlo_lock);
}

void c2_timer_attach(struct c2_timer *timer, struct c2_timer_locality *loc)
{
	C2_PRE(timer != NULL);
	C2_PRE(timer->t_info != NULL);
	C2_PRE(loc != NULL);
	C2_PRE(timer->t_info->ti_sighandler == NULL);

	timer->t_info->ti_sighandler = loc->tlo_sighandler;
	c2_atomic64_inc(&loc->tlo_sighandler->tsh_timers_count);
}

uint32_t c2_timer_locality_max()
{
	return ~0;
}

uint32_t c2_timer_locality_count()
{
	return c2_atomic64_get(&timer_locality_count);
}

struct c2_timer_info *timer_info(struct c2_rbtree_link *link)
{
	if (link == NULL)
		return NULL;
	return container_of(link, struct c2_timer_info, ti_linkage);
}

void timer_pqueue_insert(struct c2_timer_sighandler *sighandler,
		struct c2_timer_info *tinfo)
{
	while (!c2_rbtree_insert(&sighandler->tsh_pqueue, &tinfo->ti_linkage))
		tinfo->ti_expire = c2_time_add(tinfo->ti_expire, timer_1ns);
}

bool timer_pqueue_remove(struct c2_timer_sighandler *sighandler,
		struct c2_timer_info *tinfo)
{
	return c2_rbtree_remove(&sighandler->tsh_pqueue, &tinfo->ti_linkage);
}

void timer_reschedule(struct c2_timer_sighandler *sighandler,
		struct c2_timer_info *tinfo)
{
	C2_ASSERT(timer_pqueue_remove(sighandler, tinfo));
	if (--tinfo->ti_left == 0)
		return;
	tinfo->ti_expire = c2_time_add(c2_time_now(), tinfo->ti_interval);
	timer_pqueue_insert(sighandler, tinfo);
}

void timer_posix_reset(struct c2_timer_sighandler *sighandler,
		c2_time_t expire, pid_t tid)
{
	struct itimerspec ts;

	if (tid != sighandler->tsh_ptimer_thread) {
		timer_posix_fini(sighandler);
		timer_posix_init(sighandler, tid);
	}
	ts.it_interval.tv_sec = 0;
	ts.it_interval.tv_nsec = 0;
	ts.it_value.tv_sec = c2_time_seconds(expire);
	ts.it_value.tv_nsec = c2_time_nanoseconds(expire);
	C2_ASSERT(timer_settime(sighandler->tsh_ptimer,
				TIMER_ABSTIME,
				&ts,
				NULL) == 0);
}

void timer_sighandler_cleanup(struct c2_timer_sighandler *sighandler)
{
	struct c2_aqueue_link *link;
	struct c2_timer_state *tstate;
	struct c2_timer_info *tinfo;

	while ((link = c2_aqueue_get(&sighandler->tsh_tstate_dealloc)) != NULL) {
		tstate = container_of(link, struct c2_timer_state, ts_linkage);
		c2_free(tstate);
	}
	while ((link = c2_aqueue_get(&sighandler->ths_tinfo_dealloc)) != NULL) {
		tinfo = container_of(link, struct c2_timer_info, ti_linkage);
		timer_info_fini(tinfo);
	}
}

void timer_cleanup()
{
	int i;

	if (c2_atomic64_add_return(&timer_deallocating, 1) != 1)
		return;
	do {
		for (i = SIGRTMIN; i < SIGRTMIN + timer_sighandler_max(); ++i)
			timer_sighandler_cleanup(timer_sighandler(i));
	} while (c2_atomic64_sub_return(&timer_deallocating, 1) != 0);
}

static void timer_state_process(struct c2_timer_sighandler *sighandler)
{
	struct c2_timer_info *tinfo;
	enum TIMER_STATE state;

	while ((tinfo = timer_state_dequeue(sighandler, &state)) != NULL) {
		switch (state) {
		case TIMER_STATE_FINI:
			c2_aqueue_put(&sighandler->ths_tinfo_dealloc,
					&tinfo->ti_aqlink);
			break;
		case TIMER_STATE_START:
			timer_pqueue_insert(sighandler, tinfo);
			break;
		case TIMER_STATE_STOP:
			timer_pqueue_remove(sighandler, tinfo);
			break;
		default:
			C2_IMPOSSIBLE("invalid state");
		}
	}
}

static void timer_expired_execute(struct c2_timer_sighandler *sighandler)
{
	pid_t current_tid;
	struct c2_rbtree_link *tlink;
	struct c2_rbtree_link *next;
	struct c2_timer_info *tinfo;

	tlink = c2_rbtree_min(&sighandler->tsh_pqueue);
	if (tlink == NULL)
		return;
	// cache current tid
	current_tid = gettid();
	do {
		tinfo = timer_info(tlink);
		next = c2_rbtree_next(tlink);

		if (c2_time_after(c2_time_now(), tinfo->ti_expire))
			break;
		if (tinfo->ti_tid != current_tid)
			continue;

		c2_rbtree_remove(&sighandler->tsh_pqueue, tlink);
		while (c2_time_after_eq(c2_time_now(), tinfo->ti_expire)) {
			tinfo->ti_callback(tinfo->ti_data);
			tinfo->ti_expire = c2_time_add(tinfo->ti_expire,
					tinfo->ti_interval);
			if (--tinfo->ti_left == 0)
				break;
		}
		if (tinfo->ti_left == 0)
			continue;
		c2_rbtree_insert(&sighandler->tsh_pqueue, tlink);
	} while ((tlink = next) != NULL);
}

static void timer_min_jump(struct c2_timer_sighandler *sighandler)
{
	struct c2_aqueue_link *ql;
	struct c2_timer_info *tinfo;
	c2_time_t zero_time;

	ql = c2_aqueue_get(&sighandler->tsh_aqueue_min);
	if (ql == NULL) {
		// no jump targets, just disable timer
		if (sighandler->tsh_ptimer_thread != 0) {
			c2_time_set(&zero_time, 0, 0);
			timer_posix_reset(sighandler,
				zero_time,
				sighandler->tsh_ptimer_thread);

		}
		return;
	}
	tinfo = container_of(ql, struct c2_timer_info, ti_aqlink_min);
	timer_posix_reset(sighandler, tinfo->ti_expire, tinfo->ti_tid);
}

/**
   Thread-safe, async-signal-safe, lock-free.
 */
static void timer_hard_sighandler(int signo)
{
	struct c2_timer_sighandler *sighandler = timer_sighandler(signo);
	struct c2_timer_info *tinfo;

	C2_ASSERT(sighandler != NULL);
	if (c2_atomic64_add_return(&sighandler->tsh_processing, 1) != 1)
		return;
	do {
		// clear min_queue
		while (c2_aqueue_get(&sighandler->tsh_aqueue_min) != NULL)
			;
		// process states
		timer_state_process(sighandler);
		// execute all expired timer callbacks (with tid == current_tid)
		timer_expired_execute(sighandler);
		// add min to min_queue
		tinfo = timer_info(c2_rbtree_min(&sighandler->tsh_pqueue));
		if (tinfo != NULL)
			c2_aqueue_put(&sighandler->tsh_aqueue_min,
					&tinfo->ti_aqlink_min);
	} while (c2_atomic64_sub_return(&sighandler->tsh_processing, 1) != 0);
	timer_min_jump(sighandler);
}

int timer_hard_init(struct c2_timer *timer)
{
	C2_PRE(timer != NULL);

	timer_cleanup();
	timer->t_info = timer_info_init(timer);

	C2_POST(timer->t_info != NULL);
	return 0;
}

void timer_hard_state_enqueue(struct c2_timer *timer, enum TIMER_STATE state)
{
	C2_PRE(timer != NULL);
	C2_PRE(timer->t_info != NULL);
	C2_PRE(timer->t_info->ti_sighandler != NULL);

	timer_cleanup();
	timer_state_enqueue(timer->t_info, TIMER_STATE_FINI);
	timer_sighandler(timer->t_info->ti_sighandler->tsh_signo);
}

int timer_hard_fini(struct c2_timer *timer)
{
	C2_PRE(timer != NULL);
	C2_PRE(timer->t_info != NULL);

	if (timer->t_info->ti_sighandler != NULL) {
		timer_hard_state_enqueue(timer, TIMER_STATE_FINI);
	} else {
		timer_info_fini(timer->t_info);
	}
	return 0;
}

int timer_hard_start(struct c2_timer *timer)
{
	C2_PRE(timer != NULL);
	C2_PRE(timer->t_info != NULL);

	timer->t_info->ti_expire =  timer->t_expire;
	timer_hard_state_enqueue(timer, TIMER_STATE_START);
	return 0;
}

int timer_hard_stop(struct c2_timer *timer)
{
	timer_hard_state_enqueue(timer, TIMER_STATE_STOP);
	return 0;
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
		while (timer->t_left > 0 && (rc = c2_nanosleep(next, &rem)) != 0) {
			next = rem;
		}
		if (timer->t_left == 0)
			break;
		timer->t_expire = c2_time_add(timer->t_expire, timer->t_interval);
		timer->t_callback(timer->t_data);
		// race condition problem here
		// if between (timer->t_left == 0) and (--timer->t_left == 0)
		// executed timer->t_left = 0; from c2_timer_stop()
		// then we have very big loop here and lock in c2_timer_stop()
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
	int rc;

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

	rc = 0;
	if (type == C2_TIMER_HARD)
		rc = timer_hard_init(timer);
	return rc;
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

	return;
}
C2_EXPORTED(c2_timer_fini);

/**
   Init data structures for hard timer
 */
int c2_timers_init()
{
	int i;

	c2_atomic64_set(&timer_locality_count, 0);
	for (i = SIGRTMIN; i <= SIGRTMAX; ++i)
		timer_sighandler_init(timer_sighandler(i), i);
	c2_time_set(&timer_1ns, 0, 1);
	c2_time_set(&timer_infinity, 86400, 0);
	c2_atomic64_set(&timer_deallocating, 0);
	return 0;
}
C2_EXPORTED(c2_timers_init);

/**
   fini() all remaining hard timer data structures
 */
void c2_timers_fini()
{
	int i;

	timer_cleanup();
	for (i = SIGRTMIN; i <= SIGRTMAX; ++i)
		timer_sighandler_fini(timer_sighandler(i));
	C2_ASSERT(c2_atomic64_get(&timer_locality_count) == 0);
	C2_ASSERT(c2_atomic64_get(&timer_deallocating) == 0);
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
