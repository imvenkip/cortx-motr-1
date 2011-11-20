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
#include "lib/thread.h" /* c2_thread */
#include "lib/assert.h" /* C2_ASSERT */
#include "lib/atomic.h" /* c2_atomic64 */
#include "lib/memory.h" /* c2_alloc */
#include "lib/queue.h"  /* c2_queue */
#include "lib/errno.h"	/* ENOMEM */
#include "lib/cdefs.h"  /* container_of */
#include "lib/rwlock.h" /* c2_rwlock */
#include "lib/arith.h"	/* min_check */
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

*/

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

// void timer_posix_init(struct c2_timer_sighandler *sighandler, pid_t tid)
// {
// 	struct sigevent se;
// 
// 	se.sigev_notify = SIGEV_THREAD_ID;
// 	se.sigev_signo = sighandler->tsh_signo;
// 	se._sigev_un._tid = tid;
// 	C2_ASSERT(timer_create(CLOCK_REALTIME, &se, &sighandler->tsh_ptimer) == 0);
// 	sighandler->tsh_ptimer_thread = tid;
// }

// void timer_posix_fini(struct c2_timer_sighandler *sighandler)
// {
// 	C2_ASSERT(timer_delete(sighandler->tsh_ptimer) == 0);
// 	sighandler->tsh_ptimer_thread = 0;
// }

int c2_timer_locality_init(struct c2_timer_locality *loc)
{
	// XXX stub
	return 0;
}

void c2_timer_locality_fini(struct c2_timer_locality *loc)
{
	// XXX stub
}

void c2_timer_thread_attach(struct c2_timer_locality *loc)
{
	// XXX stub
}

void c2_timer_attach(struct c2_timer *timer, struct c2_timer_locality *loc)
{
	// XXX stub
}

uint32_t c2_timer_locality_max()
{
	return ~0;
}

uint32_t c2_timer_locality_count()
{
	// return c2_atomic64_get(&timer_locality_count);
	// XXX stub
	return 0;
}

void timer_pqueue_insert(struct c2_timer_sighandler *sighandler,
		struct c2_timer_info *tinfo)
{
}

bool timer_pqueue_remove(struct c2_timer_sighandler *sighandler,
		struct c2_timer_info *tinfo)
{
	// XXX stub
	return false;
}

void timer_reschedule()
	// 	struct c2_timer_sighandler *sighandler,
	// 	struct c2_timer_info *tinfo)
{
// 	C2_ASSERT(timer_pqueue_remove(sighandler, tinfo));
// 	if (--tinfo->ti_left == 0)
// 		return;
// 	tinfo->ti_expire = c2_time_add(c2_time_now(), tinfo->ti_interval);
// 	timer_pqueue_insert(sighandler, tinfo);
}

void timer_posix_reset()
// 		struct c2_timer_sighandler *sighandler,
// 		c2_time_t expire, pid_t tid)
{
// 	struct itimerspec ts;
// 
// 	if (tid != sighandler->tsh_ptimer_thread) {
// 		timer_posix_fini(sighandler);
// 		timer_posix_init(sighandler, tid);
// 	}
// 	ts.it_interval.tv_sec = 0;
// 	ts.it_interval.tv_nsec = 0;
// 	ts.it_value.tv_sec = c2_time_seconds(expire);
// 	ts.it_value.tv_nsec = c2_time_nanoseconds(expire);
// 	C2_ASSERT(timer_settime(sighandler->tsh_ptimer,
// 				TIMER_ABSTIME,
// 				&ts,
// 				NULL) == 0);
}

// XXX unused
void timer_state_process()
{
//	struct c2_timer_info *tinfo;
//	enum TIMER_STATE state;
//
//	while ((tinfo = timer_state_dequeue(sighandler, &state)) != NULL) {
//		switch (state) {
//		case TIMER_STATE_FINI:
//			// c2_aqueue_put(&sighandler->ths_tinfo_dealloc,
//					// &tinfo->ti_aqlink);
//			break;
//		case TIMER_STATE_START:
//			timer_pqueue_insert(sighandler, tinfo);
//			break;
//		case TIMER_STATE_STOP:
//			timer_pqueue_remove(sighandler, tinfo);
//			break;
//		default:
//			C2_IMPOSSIBLE("invalid state");
//		}
//	}
}

int timer_hard_init(struct c2_timer *timer)
{
	C2_PRE(timer != NULL);

	// timer->t_info = timer_info_init(timer);

	// C2_POST(timer->t_info != NULL);
	return 0;
}

// void timer_hard_state_enqueue(struct c2_timer *timer, enum TIMER_STATE state)
// {
// 	C2_PRE(timer != NULL);
// 	C2_PRE(timer->t_info != NULL);
// 	C2_PRE(timer->t_info->ti_sighandler != NULL);
// 
// 	timer_cleanup();
// 	timer_state_enqueue(timer->t_info, TIMER_STATE_FINI);
// 	timer_sighandler(timer->t_info->ti_sighandler->tsh_signo);
// }

int timer_hard_fini(struct c2_timer *timer)
{
// 	C2_PRE(timer != NULL);
// 	C2_PRE(timer->t_info != NULL);
// 
// 	if (timer->t_info->ti_sighandler != NULL) {
// 		timer_hard_state_enqueue(timer, TIMER_STATE_FINI);
// 	} else {
// 		timer_info_fini(timer->t_info);
// 	}
	return 0;
}

int timer_hard_start(struct c2_timer *timer)
{
// 	C2_PRE(timer != NULL);
// 	C2_PRE(timer->t_info != NULL);
// 
// 	timer->t_info->ti_expire =  timer->t_expire;
// 	timer_hard_state_enqueue(timer, TIMER_STATE_START);
	return 0;
}

int timer_hard_stop(struct c2_timer *timer)
{
	// timer_hard_state_enqueue(timer, TIMER_STATE_STOP);
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
