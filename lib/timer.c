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

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/mutex.h"
#include "lib/cond.h"
#include "lib/thread.h"
#include "lib/assert.h"
#include "lib/atomic.h" /* c2_atomic64 */
#include "lib/memory.h" /* c2_alloc */
#include "lib/queue.h"  /* c2_queue */
#include "lib/errno.h"	/* ENOMEM */
#include "lib/rbtree.h" /* c2_rbtree */
#include "lib/cdefs.h"  /* container_of */
#include "lib/rwlock.h" /* c2_rwlock */

#include "lib/timer.h"

#include <stdio.h>
/**
   @addtogroup timer

   Implementation of c2_timer.

   In userspace timer implementation, there is a timer thread running,
   which checks the expire time and trigger timer callback if needed.
   There is one timer thread for each timer.

   hard timer
	rbtree timer_queue (key: expiration time)
	queue state_queue (init: empty)
	semaphore for state queue - items count in queue (init: 0)
	atomic sigcount (init 0)

   sighandler
	if inc(sigcount) != 1
		read next from state_queue
			start: add timer to timer_queue
			stop: remove timer from timer_queue
		for every expired timer
			if timer must run from current thread - execute callback
			else switch to destination thread and exit from sighandler



   ------------------------- outdated below --------------------
   There is a problem with signal handler - it is the same across whole process.

   threads_map_mutex
   threads map
	thread_info
		id
		thread_info_mutex
		callback queue mutex
		callback queue
		timer_hard_count
   timers priority queue
	c2_timer_info
		*c2_timer
		*timer_thread_info
		started
		atomic ref_count
	c2_timer
		*c2_timer_info
   timers_state_queue
	timer_state
		link (for c2_queue)
		*timer
		state
   callbacks queue for every thread
	timer_callback_info
		callback
		data

   init			(timer_hard_init)
	increment total_timer_hard_count
	lock thread_map_mutex
	get thread_info for calling thread
	if not exists
		add thread_info entry
		install thread handler for SIGUSR1
	increment thread_info.timer_hard_count
	unlock thread_map_mutex
	alloc timer_info
	set timer_info.started to false
	start hard time scheduler if it is first timer

   fini			(timer_hard_fini)
	queue state TIMER_STATE_FINI

   start		(timer_hard_start)
	queue state TIMER_STATE_START
   stop			(timer_hard_stop)
	queue state TIMER_STATE_STOP
   enqueue state	(timer_state_enqueue)
	atomically add timer_state to timers_status_queue
	wake up timer scheduling thread
	

   scheduler		(timer_scheduling_thread)
	set SIGUSR1 handler to scheduler signal handler
	while exists some hard timers
		enqueue state TIMER_STATE_EXPIRED
		get nearest expiration time
		nanosleep this time

   scheduler signal handler	(timer_scheduler_sighandler)
	while timers_status_queue not empty - process next entry
		switch new_status
			start: add to priority queue, set timer_info.started to true
			stop: set timer_info.started to false, remove from priority queue
			fini: dealloc timer_info
			      send SIGUSR1 to timer thread otherwise
			expired: for every expired timer:
				remove from priority queue, add to thread timer queue,
				send SIGUSR1 to destination timer, recalc next expiration
				time, add to priority queue
	
   user thread signal handler	(timer_destination_sighandler) 
	atomically get thread_info for calling thread
	while thread_timers_queue not empty
		dequeue callback_info
		call callback
   @{
*/

// TODO cache timer_next_expiration after enqueue

enum TIMER_STATE {
	TIMER_STATE_START,
	TIMER_STATE_STOP,
	TIMER_STATE_FINI,
	TIMER_STATE_EXPIRED
};

struct timer_callback_info {
	struct c2_queue_link tci_linkage;
	c2_timer_callback_t tci_callback;
	unsigned long tci_data;
};

struct timer_state {
	struct c2_queue_link ts_linkage;
	enum TIMER_STATE ts_state;
	struct c2_timer_info *ts_timer;	
};

// FIXME rename
struct timer_thread_info {
	struct c2_rbtree_link tti_linkage;
	pthread_t tti_id;
	struct c2_mutex tti_queue_lock;
	// SPSC queue
	// producer - hard timer scheduling thread (timer_scheduling_thread)
	// consumer - SUGUSR1 handler in destination thread
	// thread safety using tti_queue_lock mutex
	struct c2_queue tti_queue;
	struct c2_atomic64 tti_timer_count;
	// for SIGUSR1 default signal handler
	struct sigaction tti_sigaction;
};

struct c2_timer_info {
	struct c2_rbtree_link ti_linkage;
	c2_time_t ti_expire;
	c2_time_t ti_interval;
	uint64_t ti_left;
	struct c2_timer *ti_timer;
	struct timer_thread_info *ti_thread;
	bool ti_started;
};

// TODO rename
static struct c2_thread timer_scheduler;
static struct c2_mutex timer_scheduler_lock;
static struct c2_atomic64 timer_hard_count;
// FIXME rename
static struct c2_atomic64 timer_scheduler_sighandler_unprocessed;
static struct c2_atomic64 timer_destination_sighandler_unprocessed;

// MPSC queue
// Produsers - all threads, that called timer_start(), timer_stop()
//	or timer_fini() + hard timer scheduling thread
// Consumer - timer_scheduler_sighandler()
// Thread safety using mutex
static struct c2_queue timer_states;
static struct c2_mutex timer_states_lock;

// Map of timer_thread_info
// Implemented as red-black tree
// Thread safety using rwlock
static struct c2_rbtree timer_threads;
static struct c2_rwlock timer_threads_lock;

// Priority queue with all running timers, sorted by expiration time
// Implemented as red-black tree
// Thread safety using only one thread for read/modify
static struct c2_rbtree timer_timers;

static c2_time_t timer_next_expiration;
static c2_time_t timer_time_one_ns;
static c2_time_t timer_time_infinity;

/**
   Empty function for SIGUSR1 sighandler for soft timer scheduling thread.
   Using for wake up thread.
 */
void nothing(int unused)
{
}

void print_time(c2_time_t t)
{
	printf("%lu.%lu",
		c2_time_seconds(t), c2_time_nanoseconds(t));
}

/**
   Add timer to priority queue, sorted by expiration time
 */
static void timer_queue_insert(struct c2_timer_info *tinfo)
{
	while (!c2_rbtree_insert(&timer_timers, &tinfo->ti_linkage))
		tinfo->ti_expire = c2_time_add(tinfo->ti_expire, timer_time_one_ns);
}

/**
   Remove timer from priority queue
 */
static void timer_queue_delete(struct c2_timer_info *timer)
{
	C2_ASSERT(c2_rbtree_remove(&timer_timers, &timer->ti_linkage));
}

/**
   Comparator for red-black tree for timers_queue
   Return
	-1 if a < b
	0  if a == b
	1  if a > b
 */
static int timer_queue_cmp(void *_a, void *_b)
{
	c2_time_t a = *(c2_time_t *) _a;
	c2_time_t b = *(c2_time_t *) _b;

	if (c2_time_after_eq(a, b) && c2_time_after_eq(b, a))
		return 0;
	else
		return c2_time_after(a, b) ? 1 : -1;
}

/**
   Get timer with nearest expiration time.
   If some timers already expired, returns timer with minimal expiration time.
   If no timers in queue, return NULL
 */
static struct c2_timer_info* timer_queue_peek()
{
	struct c2_rbtree_link *link = c2_rbtree_min(&timer_timers);
	struct c2_timer_info *tinfo;

	if (link == NULL)
		return NULL;

	tinfo = container_of(link, struct c2_timer_info, ti_linkage);
	return tinfo;
}

/**
   Get first expired hard timer from the timer queue
   Return NULL if no expired timers or no timers in queue
 */
static struct c2_timer_info* timer_expired_peek()
{
	struct c2_timer_info *tinfo = timer_queue_peek();
	c2_time_t now = c2_time_now();

	if (tinfo == NULL)
		return NULL;
	return c2_time_after(now, tinfo->ti_expire) ? tinfo : NULL;
}

/**
   Get nearest hard timer expiration time
   If expired timer already exists, return zero time
   If no timers in queue, return timer_time_infinity
 */
static c2_time_t timer_next_expiration_get()
{
	struct c2_timer_info *tinfo = timer_queue_peek();
	c2_time_t now = c2_time_now();
	c2_time_t rem;

	if (tinfo == NULL)
		return timer_time_infinity;

	if (c2_time_after(now, tinfo->ti_expire))
		c2_time_set(&rem, 0, 0);
	else
		rem = c2_time_sub(tinfo->ti_expire, now);

	return rem;
}

/**
   Add timer to priority queue, sorted by expiration time.
   Implementation based on red-black tree.
 */
static void timer_state_enqueue(struct c2_timer_info *tinfo, enum TIMER_STATE state)
{
	struct timer_state *tstate = c2_alloc(sizeof *tstate);
	C2_ASSERT(tstate != NULL);

	tstate->ts_timer = tinfo;
	tstate->ts_state = state;
	c2_queue_link_init(&tstate->ts_linkage);

	c2_mutex_lock(&timer_states_lock);
	c2_queue_put(&timer_states, &tstate->ts_linkage);
	c2_mutex_unlock(&timer_states_lock);
}

/**
   Remove timer from priority queue, sorted by expiration time.
 */
static bool timer_state_dequeue(struct c2_timer_info **tinfo, enum TIMER_STATE *state)
{
	struct timer_state *tstate;
	struct c2_queue_link *link;

	c2_mutex_lock(&timer_states_lock);
	link = c2_queue_get(&timer_states);
	c2_mutex_unlock(&timer_states_lock);

	if (link == NULL)
		return false;

	tstate = container_of(link, struct timer_state, ts_linkage);
	c2_queue_link_fini(&tstate->ts_linkage);
	*state = tstate->ts_state;
	*tinfo = tstate->ts_timer;
	c2_free(tstate);
	c2_thread_signal(&timer_scheduler, SIGUSR1);
	return true;
}

/*
   Add callback to callback queue (one queue for every user thread)
 */
static void timer_callback_enqueue(struct c2_timer_info *tinfo)
{
	struct timer_thread_info *thread_info;
	struct c2_timer *timer;
	struct timer_callback_info *callback;

	C2_PRE(tinfo != NULL);
	thread_info = tinfo->ti_thread;
	C2_ASSERT(thread_info != NULL);
	timer = tinfo->ti_timer;
	C2_ASSERT(timer != NULL);

	callback = c2_alloc(sizeof *callback);
	C2_ASSERT(callback != NULL);

	callback->tci_callback = timer->t_callback;
	callback->tci_data = timer->t_data;
	c2_queue_link_init(&callback->tci_linkage);

	c2_mutex_lock(&thread_info->tti_queue_lock);
	c2_queue_put(&thread_info->tti_queue, &callback->tci_linkage);
	c2_mutex_unlock(&thread_info->tti_queue_lock);
}

/*
   Remove callback from callback queue
 */
static bool timer_callback_dequeue(struct timer_thread_info *thread_info, struct timer_callback_info *info)
{
	printf("timer_callback_dequeue\n");
	struct timer_callback_info *callback;
	struct c2_queue_link *link;

	C2_PRE(thread_info != NULL);
	C2_PRE(info != NULL);

	c2_mutex_lock(&thread_info->tti_queue_lock);
	link = c2_queue_get(&thread_info->tti_queue);
	c2_mutex_unlock(&thread_info->tti_queue_lock);

	if (link == NULL)
		return false;

	printf("exists item for dequeue\n");
	callback = container_of(link, struct timer_callback_info, tci_linkage);
	c2_queue_link_fini(&callback->tci_linkage);
	info->tci_callback = callback->tci_callback;
	info->tci_data = callback->tci_data;
	c2_free(callback);
	return true;
}

static void timer_destination_sighandler(int signum);

/**
   init() for timer_thread_info structure.
   timer_thread_info allocated when adding first hard timer in thread
   and deallocated when removing last
 */
static struct timer_thread_info *thread_info_init(pthread_t id)
{
	struct timer_thread_info *thread_info = c2_alloc(sizeof *thread_info);
	struct sigaction sa;
	C2_ASSERT(thread_info != NULL);

	c2_rbtree_link_init(&thread_info->tti_linkage);
	thread_info->tti_id = id;
	c2_mutex_init(&thread_info->tti_queue_lock);
	c2_queue_init(&thread_info->tti_queue);
	c2_atomic64_set(&thread_info->tti_timer_count, 0);
	sa.sa_handler = timer_destination_sighandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NODEFER;
	printf("installing signal handler from TID = %lu\n", pthread_self());
	sigaction(SIGUSR1, &sa, &thread_info->tti_sigaction);

	return thread_info;
}

/**
   fini() for timer_thread_info structure.
 */
static void thread_info_fini(struct timer_thread_info *thread_info)
{
	C2_ASSERT(thread_info != NULL);
	C2_ASSERT(c2_atomic64_get(&thread_info->tti_timer_count) == 0);

	sigaction(SIGUSR1, &thread_info->tti_sigaction, NULL);
	c2_queue_fini(&thread_info->tti_queue);
	c2_mutex_fini(&thread_info->tti_queue_lock);
	c2_rbtree_link_fini(&thread_info->tti_linkage);
}

/**
   Get timer_thread_info for calling process.
   Allocate and add it to struct timer_threads if timer_thread_info not
   exists for current thread.
 */
static struct timer_thread_info *thread_info_self()
{
	struct timer_thread_info *info;
	pthread_t id = pthread_self();
	struct c2_rbtree_link *link;

	c2_rwlock_read_lock(&timer_threads_lock);
	link = c2_rbtree_find(&timer_threads, (void *) &id);
	c2_rwlock_read_unlock(&timer_threads_lock);

	if (link != NULL)
		return container_of(link, struct timer_thread_info, tti_linkage);

	c2_rwlock_write_lock(&timer_threads_lock);
	link = c2_rbtree_find(&timer_threads, (void *) &id);
	if (link == NULL) {
		info = thread_info_init(pthread_self());
		C2_ASSERT(info != NULL);

		link = &info->tti_linkage;
		C2_ASSERT(c2_rbtree_insert(&timer_threads, link));
	}
	c2_rwlock_write_unlock(&timer_threads_lock);

	return container_of(link, struct timer_thread_info, tti_linkage);
}

/**
   Free timer_thread_info structure if no hard timers left for thread
   Do noting otherwise
 */
static void thread_info_tryfree(struct timer_thread_info *info)
{
	C2_ASSERT(info != NULL);

	if (c2_atomic64_get(&info->tti_timer_count) != 0)
		return;

	c2_rwlock_write_lock(&timer_threads_lock);
	if (c2_atomic64_get(&info->tti_timer_count) == 0) {
		C2_ASSERT(c2_rbtree_remove(&timer_threads, &info->tti_linkage));
		thread_info_fini(info);
	}
	c2_rwlock_write_unlock(&timer_threads_lock);
}

static void timer_scheduling_thread(void *unused);

/**
   init() for c2_timer_info structure
 */
static struct c2_timer_info *timer_info_init(struct c2_timer *timer)
{
	struct c2_timer_info *tinfo = c2_alloc(sizeof *tinfo);
	struct timer_thread_info *thread_info;
	int rc;

	C2_ASSERT(timer != NULL);
	C2_ASSERT(tinfo != NULL);

	thread_info = thread_info_self();
	C2_ASSERT(thread_info != NULL);

	c2_rbtree_link_init(&tinfo->ti_linkage);
	tinfo->ti_interval = timer->t_interval;
	tinfo->ti_timer = timer;
	timer->t_info = tinfo;
	tinfo->ti_thread = thread_info;
	c2_atomic64_inc(&thread_info->tti_timer_count);
	tinfo->ti_started = false;

	c2_mutex_lock(&timer_scheduler_lock);
	// start hard timer scheduling thread if necessary
	if (c2_atomic64_add_return(&timer_hard_count, 1) == 1) {
		rc = C2_THREAD_INIT(&timer_scheduler, void*, NULL,
				    &timer_scheduling_thread,
				    NULL, "c2_timer_scheduler");
	} else {
		rc = timer_scheduler.t_state == TS_PARKED;
	}
	c2_mutex_unlock(&timer_scheduler_lock);

	C2_ASSERT(rc == 0);
	
	return tinfo;

}

/**
   fini() for c2_timer_info structure
 */
static void timer_info_fini(struct c2_timer_info *tinfo)
{
	C2_ASSERT(tinfo != NULL);

	c2_atomic64_dec(&tinfo->ti_thread->tti_timer_count);
	thread_info_tryfree(tinfo->ti_thread);
	c2_rbtree_link_fini(&tinfo->ti_linkage);
	c2_free(tinfo);

	// stop scheduler thread if no hard timers left
	c2_mutex_lock(&timer_scheduler_lock);
	if (c2_atomic64_sub_return(&timer_hard_count, 1) == 0) {
		c2_thread_signal(&timer_scheduler, SIGUSR1);
		c2_thread_join(&timer_scheduler);
		c2_thread_fini(&timer_scheduler);
	}
	c2_mutex_unlock(&timer_scheduler_lock);
}

/**
   Comparator for timer_threads map
 */
static int timer_tid_cmp(void *_a, void *_b)
{
	pthread_t a = *(pthread_t *) _a;
	pthread_t b = *(pthread_t *) _b;

	if (a == b)
		return 0;
	else
		return a > b ? 1 : -1;
}

/**
   SIGUSR1 signal handler in destination thread.
   It reads all callback from callbacks queue and executes them
 */
static void timer_destination_sighandler(int signum)
{
	printf("------------------ destination SIGUSR1 catched, ");
	printf("TID = %lu\n", pthread_self());
	struct timer_thread_info *info;
	struct timer_callback_info cinfo;

	if (c2_atomic64_add_return(&timer_destination_sighandler_unprocessed, 1) != 1)
		return;

	info = thread_info_self();
	C2_ASSERT(info != NULL);

	do {
		while (timer_callback_dequeue(info, &cinfo))
			cinfo.tci_callback(cinfo.tci_data);
	} while (c2_atomic64_sub_return(&timer_destination_sighandler_unprocessed, 1) != 0);
}

/**
   Process timer state.
   This function called only from SIGUSR1 handler in hard timers scheduling thread
   insert/delete from timers priority queue are only there, so there is no need for
   any type of synchronization.
 */
static void timer_state_process(struct c2_timer_info *tinfo, enum TIMER_STATE state)
{
	printf("timer_state_process(), state = %d\n", (int) state);
	switch (state) {
	case TIMER_STATE_START:
		printf("TIMER_STATE_START\n");
		printf("%lu\n", tinfo->ti_left);
		if (tinfo->ti_left == 0)
			break;
		tinfo->ti_started = true;
		printf("timer_queue_insert: expiration on "); 
		print_time(tinfo->ti_expire);
		printf(", current time ");
		print_time(c2_time_now());
		printf("\n");
		timer_queue_insert(tinfo);
		break;
	case TIMER_STATE_STOP:
		printf("TIMER_STATE_STOP\n");
		timer_queue_delete(tinfo);
		tinfo->ti_left = 0;
		tinfo->ti_started = false;
		break;
	case TIMER_STATE_FINI:
		printf("TIMER_STATE_FINI\n");
		timer_info_fini(tinfo);
		break;
	case TIMER_STATE_EXPIRED:
		printf("TIMER_STATE_EXPIRED\n");
		while ((tinfo = timer_expired_peek()) != NULL) {
			timer_queue_delete(tinfo);
			tinfo->ti_expire = c2_time_add(tinfo->ti_expire, tinfo->ti_interval);
			print_time(tinfo->ti_expire);
			printf(" ");
			print_time(c2_time_now());
			printf(" ");
			printf("%lu\n", tinfo->ti_left);
			if (--tinfo->ti_left == 0)
				continue;
			// add callback to destination thread callback queue
			timer_callback_enqueue(tinfo);
			// call SIGUSR1 handler in destinatiin thread, which
			// will dequeue and execute callback
			C2_ASSERT(pthread_kill(tinfo->ti_thread->tti_id, SIGUSR1) == 0);
			timer_queue_insert(tinfo);
		}
		break;
	}
	timer_next_expiration = timer_next_expiration_get();
	printf("next expiration in %lu.%lu\n",
		c2_time_seconds(timer_next_expiration), c2_time_nanoseconds(timer_next_expiration));
}

/**
   SIGUSR1 handler for hard timers scheduling thread.
 */
static void timer_scheduler_sighandler(int signum)
{
	static struct c2_timer_info *timer;
	static enum TIMER_STATE state;

	printf("SIGUSR1 catched, ");
	printf("TID = %lu\n", pthread_self());
	if (c2_atomic64_add_return(&timer_scheduler_sighandler_unprocessed, 1) != 1)
		return;
	do {
		while (timer_state_dequeue(&timer, &state))
			timer_state_process(timer, state);
	} while (c2_atomic64_sub_return(&timer_scheduler_sighandler_unprocessed, 1) != 0);
}

/**
   Hard timers sheduling thread.
 */
static void timer_scheduling_thread(void *unused)
{
	printf("start of timer scheduling thread, TID = %lu\n", pthread_self());
	struct sigaction sa;
	// signal(SIGUSR1, timer_scheduler_sighandler);
	sa.sa_handler = timer_scheduler_sighandler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NODEFER;
	printf("installing signal handler from TID = %lu\n", pthread_self());
	sigaction(SIGUSR1, &sa, NULL);

	while (c2_atomic64_get(&timer_hard_count) != 0) {
		timer_state_enqueue(NULL, TIMER_STATE_EXPIRED);
		// c2_thread_signal(&timer_scheduler, SIGUSR1);
		timer_scheduler_sighandler(SIGUSR1);

		// wait for next timer expiration
		// FIXME no wait if no timers left
		printf("nanosleep begin\n");
		c2_nanosleep(timer_next_expiration, NULL);
		printf("nanosleep end\n");
	}
	timer_scheduler_sighandler(SIGUSR1);
	printf("finish of timer scheduling thread\n");
}

/**
   init() function for hard timer
   Start hard timer scheduling thread if it is first timer
 */
static int timer_hard_init(struct c2_timer *timer)
{
	printf("timer_hard_init(), timer = %p\n", timer);
	return !timer_info_init(timer);
}

/**
   fini() function for hard timer
   Stop hard timer scheduling thread if it is last timer
 */
static void timer_hard_fini(struct c2_timer *timer)
{
	printf("timer_hard_fini(), timer = %p\n", timer);
	timer_state_enqueue(timer->t_info, TIMER_STATE_FINI);
}

/**
   Start a hard timer
   t_expiration and t_left already set in timer_start()
 */
static int timer_hard_start(struct c2_timer *timer)
{
	printf("timer_hard_start(), timer = %p\n", timer);
	// t_expire and t_left already set in timer_start()
	printf("%lu\n", timer->t_left);
	timer->t_info->ti_left = timer->t_left;
	timer->t_info->ti_expire = timer->t_expire;

	timer_state_enqueue(timer->t_info, TIMER_STATE_START);
	return 0;
}

/**
   Stop a hard timer
 */
static int timer_hard_stop(struct c2_timer *timer)
{
	printf("timer_hard_stop(), timer = %p\n", timer);
	timer_state_enqueue(timer->t_info, TIMER_STATE_STOP);
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
	c2_mutex_init(&timer_scheduler_lock);
	c2_atomic64_set(&timer_hard_count, 0);
	c2_atomic64_set(&timer_scheduler_sighandler_unprocessed, 0);
	c2_atomic64_set(&timer_destination_sighandler_unprocessed, 0);
	c2_queue_init(&timer_states);
	c2_mutex_init(&timer_states_lock);
	c2_rbtree_init(&timer_threads, timer_tid_cmp,
			offsetof(struct timer_thread_info, tti_id) -
			offsetof(struct timer_thread_info, tti_linkage));
	c2_rwlock_init(&timer_threads_lock);
	c2_rbtree_init(&timer_timers, timer_queue_cmp,
			offsetof(struct c2_timer_info, ti_expire) -
			offsetof(struct c2_timer_info, ti_linkage));
	c2_time_set(&timer_time_one_ns, 0, 1);
	c2_time_set(&timer_time_infinity, 1000000000000ULL, 0);

	return 0;
}
C2_EXPORTED(c2_timers_init);

/**
   fini() all remaining hard timer data structures
 */
void c2_timers_fini()
{
	c2_rbtree_fini(&timer_timers);
	c2_rwlock_fini(&timer_threads_lock);
	c2_rbtree_fini(&timer_threads);
	c2_mutex_fini(&timer_states_lock);
	c2_queue_fini(&timer_states);
	c2_mutex_fini(&timer_scheduler_lock);
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
