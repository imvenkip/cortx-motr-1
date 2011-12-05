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

#include "lib/ut.h"
#include "lib/time.h"
#include "lib/timer.h"
#include "lib/assert.h"
#include "lib/thread.h"
#include "lib/mutex.h"
#include "lib/semaphore.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/syscall.h>

enum {
	MANY_TICKS_NR = 1000,
	NR_TIMERS = 10,
	NR_TICKS = 10,
	NR_TG = 10,		/* number of thread groups */
	NR_THREADS_TG = 10,	/* number of slave threads per thread group */
	NR_TIMERS_TG = 100	/* number of timers per thread group */
};

struct thread_group;

struct tg_slave {
	pid_t                tgs_tid;
	struct c2_semaphore  tgs_sem_init;
	struct c2_semaphore  tgs_sem_resume;
	struct thread_group *tgs_group;
};


struct tg_timer {
	struct c2_timer	     tgt_timer;
	struct thread_group *tgt_group;
	c2_time_t	     tgt_interval;
	uint64_t	     tgt_repeat;
	uint64_t	     tgt_left;
	struct c2_semaphore  tgt_done;
};

struct thread_group {
	struct c2_thread	 tg_master;
	struct c2_thread	 tg_threads[NR_THREADS_TG];
	struct tg_slave		 tg_slaves[NR_THREADS_TG];
	struct c2_semaphore	 tg_sem_init;
	struct c2_semaphore	 tg_sem_resume;
	struct c2_semaphore	 tg_sem_done;
	struct tg_timer		 tg_timers[NR_TIMERS_TG];
	struct c2_timer_locality tg_loc;
	unsigned int		 tg_seed;
};

static int count = 0;
static int verbose = 0;

static pid_t		   loc_default_tid;
static struct c2_semaphore loc_default_lock;

static struct c2_semaphore many_finished[NR_TIMERS];
static int		   many_iterations[NR_TIMERS];
static int		   many_iterations_max[NR_TIMERS];
static pid_t		   many_pids[NR_TIMERS];

unsigned long tick(unsigned long data)
{
	c2_time_t now;

	now = c2_time_now();
	count ++;
	if (verbose)
		printf("%lu.%lu: timer1 tick = %d\n",
		       c2_time_seconds(now), c2_time_nanoseconds(now), count);

	return 0;
}

unsigned long tack(unsigned long data)
{
	static int tack;
	c2_time_t  now;

	now = c2_time_now();
	tack += data;
	if (verbose)
		printf("%lu.%lu:    timer2 tack = %d\n",
			c2_time_seconds(now), c2_time_nanoseconds(now), tack);

	return 0;
}

void test_2_timers()
{
	struct c2_timer timer1;
	struct c2_timer timer2;
	c2_time_t       i1;
	c2_time_t       i2;
	c2_time_t       wait;

	c2_time_set(&i1, 1, 0);
	c2_timer_init(&timer1, C2_TIMER_SOFT, i1, 5, tick, 0);
	c2_timer_start(&timer1);

	c2_time_set(&i2, 3, 0);
	c2_timer_init(&timer2, C2_TIMER_SOFT, i2, 5, tack, 10000);
	c2_timer_start(&timer2);

	c2_time_set(&wait, 0, 500000000); /* 0.5 second */
	/* let's do something, e.g. just waiting */
	while (count < 5)
		c2_nanosleep(wait, NULL);
	c2_timer_stop(&timer1);

	/* start timer1 again, and do something, e.g. just waiting */
	c2_timer_start(&timer1);
	while (count < 8)
		c2_nanosleep(wait, NULL);
	c2_timer_stop(&timer2);
	c2_timer_fini(&timer2);

	while (count < 10)
		c2_nanosleep(wait, NULL);

	c2_timer_stop(&timer1);
	c2_timer_fini(&timer1);
}

void timer1_thread(int unused)
{
	struct c2_timer timer1;
	c2_time_t       i1;
	c2_time_t       wait;

	c2_time_set(&i1, 1, 0);
	c2_timer_init(&timer1, C2_TIMER_SOFT, i1, 5, tick, 0);
	c2_timer_start(&timer1);

	c2_time_set(&wait, 0, 500000000); /* 0.5 second */
	/* let's do something, e.g. just waiting */
	while (count < 5)
		c2_nanosleep(wait, NULL);

	/* stop and start timer1 again, and do something, e.g. just waiting */
	c2_timer_stop(&timer1);
	c2_timer_start(&timer1);

	while (count < 10)
		c2_nanosleep(wait, NULL);

	c2_timer_stop(&timer1);
	c2_timer_fini(&timer1);
	if (verbose)
		printf("timer1 thread exit\n");
}

void timer2_thread(int unused)
{
	struct c2_timer timer2;
	c2_time_t       i2;
	c2_time_t       wait;

	c2_time_set(&i2, 3, 0);
	c2_timer_init(&timer2, C2_TIMER_SOFT, i2, 5, tack, 10000);
	c2_timer_start(&timer2);

	c2_time_set(&wait, 0, 500000000); /* 0.5 second */
	while (count < 8)
		c2_nanosleep(wait, NULL);
	c2_timer_stop(&timer2);
	c2_timer_fini(&timer2);
	if (verbose)
		printf("timer2 thread exit\n");
}

void test_timer_soft(void)
{
	struct c2_thread t1 = { 0 };
	struct c2_thread t2 = { 0 };
	int rc;

	verbose = 1;

	printf("start timer testing... this takes about 20 seconds\n");
	test_2_timers();

	if (verbose)
		printf("==================\nstart 2 timers in 2 threads ...\n");

	count = 0;

	rc = C2_THREAD_INIT(&t1, int, NULL, &timer1_thread, 0, "timer1_thread");
	C2_ASSERT(rc == 0);
	rc = C2_THREAD_INIT(&t2, int, NULL, &timer2_thread, 0, "timer2_thread");
	C2_ASSERT(rc == 0);

	c2_thread_join(&t1);
	c2_thread_fini(&t1);
	c2_thread_join(&t2);
	c2_thread_fini(&t2);

	if (verbose)
		printf("end timer testing...\n");

	verbose = 0;
	return;
}

static pid_t gettid()
{
	return syscall(SYS_gettid);
}

static unsigned long loc_default_callback(unsigned long data)
{
	C2_UT_ASSERT(gettid() == loc_default_tid);
	c2_semaphore_up(&loc_default_lock);
	return 0;
}

static void test_timer_locality_default()
{
	c2_time_t	interval;
	int		rc;
	struct c2_timer timer;

	c2_time_set(&interval, 0, 100000000);	/* .1 sec */
	rc = c2_timer_init(&timer, C2_TIMER_HARD, interval,
			1, &loc_default_callback, 0);
	C2_ASSERT(rc == 0);

	rc = c2_semaphore_init(&loc_default_lock, 0);
	C2_ASSERT(rc == 0);

	loc_default_tid = gettid();
	rc = c2_timer_start(&timer);
	C2_ASSERT(rc == 0);
	c2_semaphore_down(&loc_default_lock);

	rc = c2_timer_stop(&timer);
	C2_ASSERT(rc == 0);

	c2_semaphore_fini(&loc_default_lock);
	c2_timer_fini(&timer);
}

static unsigned long many_callback(unsigned long data)
{
	C2_ASSERT(data >= 0 && data < NR_TIMERS);
	C2_ASSERT(many_pids[data] == gettid());
	if (++many_iterations[data] == many_iterations_max[data])
		c2_semaphore_up(&many_finished[data]);
	return 0;
}

static void test_timer_many_timers(int timers_nr, int iter_nr)
{
	int                      i;
	int                      rc;
	static struct c2_timer   timer[NR_TIMERS];
	struct c2_timer_locality loc;
	c2_time_t		 interval;

	C2_ASSERT(timers_nr <= NR_TIMERS);

	for (i = 0; i < timers_nr; ++i) {
		c2_semaphore_init(&many_finished[i], 0);
		many_iterations[i] = 0;
		many_iterations_max[i] = iter_nr;
		many_pids[i] = gettid();
	}

	c2_timer_locality_init(&loc);
	c2_timer_thread_attach(&loc);
	c2_time_set(&interval, 0, 100000);

	for (i = 0; i < timers_nr; ++i) {
		rc = c2_timer_init(&timer[i], C2_TIMER_HARD, interval,
				many_iterations_max[i], &many_callback, i);
		C2_ASSERT(rc == 0);
		c2_timer_attach(&timer[i], &loc);
	}

	for (i = 0; i < timers_nr; ++i)
		c2_timer_start(&timer[i]);

	for (i = 0; i < timers_nr; ++i)
		c2_semaphore_down(&many_finished[i]);

	for (i = 0; i < timers_nr; ++i) {
		c2_timer_stop(&timer[i]);
		c2_timer_fini(&timer[i]);
		c2_semaphore_fini(&many_finished[i]);
		C2_UT_ASSERT(many_iterations[i] == iter_nr);
	}

	c2_timer_thread_detach(&loc);
	c2_timer_locality_fini(&loc);
}

static void sem_init_zero(struct c2_semaphore *sem)
{
	int rc = c2_semaphore_init(sem, 0);
	C2_ASSERT(rc == 0);
}

/*
 * It isn't safe to use C2_UT_ASSERT in signal handler code,
 * therefore instead of C2_UT_ASSERT used C2_ASSERT.
 */
static unsigned long test_timer_callback_mt(unsigned long data)
{
	struct tg_timer *tgt = (struct tg_timer *) data;
	bool		 found = false;
	pid_t		 tid = gettid();
	int		 i;

	/* check thread ID */
	for (i = 0; i < NR_THREADS_TG; ++i)
		if (tgt->tgt_group->tg_slaves[i].tgs_tid == tid) {
			found = true;
			break;
		}
	C2_ASSERT(found);
	/* check number of callback executed for this timer */
	C2_ASSERT(tgt->tgt_left > 0);
	/* if no callbacks left - `up' tg_timer semaphore */
	if (--tgt->tgt_left == 0)
		c2_semaphore_up(&tgt->tgt_done);
	return 0;
}

static void test_timer_slave_mt(struct tg_slave *slave)
{
	slave->tgs_tid = gettid();
	/* add slave thread to locality */
	c2_timer_thread_attach(&slave->tgs_group->tg_loc);
	/* signal to master thread about init */
	c2_semaphore_up(&slave->tgs_sem_init);

	/* now c2_timer callback can be executed in this thread context */

	/* wait for master thread */
	c2_semaphore_down(&slave->tgs_sem_resume);
	/* remove current thread from thread group locality */
	c2_timer_thread_detach(&slave->tgs_group->tg_loc);
}

static void test_timer_master_mt(struct thread_group *tg)
{
	int		 i;
	int		 rc;
	struct tg_timer *tgt;

	/* init() all slave semaphores */
	for (i = 0; i < NR_THREADS_TG; ++i) {
		sem_init_zero(&tg->tg_slaves[i].tgs_sem_init);
		sem_init_zero(&tg->tg_slaves[i].tgs_sem_resume);
	}
	/* init() timer locality */
	c2_timer_locality_init(&tg->tg_loc);
	/* init() and start all slave threads */
	for (i = 0; i < NR_THREADS_TG; ++i) {
		tg->tg_slaves[i].tgs_group = tg;
		rc = C2_THREAD_INIT(&tg->tg_threads[i], struct tg_slave *,
				NULL, &test_timer_slave_mt,
				&tg->tg_slaves[i], "timer test slave");
		C2_ASSERT(rc == 0);
	}
	/* wait until all slaves initialized */
	for (i = 0; i < NR_THREADS_TG; ++i)
		c2_semaphore_down(&tg->tg_slaves[i].tgs_sem_init);
	/* init() all timers */
	for (i = 0; i < NR_TIMERS_TG; ++i) {
		tgt = &tg->tg_timers[i];
		tgt->tgt_group = tg;
		/* interval is random in [1, 100] ms range */
		c2_time_set(&tgt->tgt_interval, 0,
				(1 + rand_r(&tg->tg_seed) % 100) * 1000000);
		/* repeat count is random in [1, 10] range */
		tgt->tgt_repeat = 1 + rand_r(&tg->tg_seed) % 10;
		tgt->tgt_left = tgt->tgt_repeat;
		/* init timer semaphore */
		rc = c2_semaphore_init(&tgt->tgt_done, 0);
		C2_ASSERT(rc == 0);
		/* `unsigned long' must have enough space to contain `void*' */
		C2_CASSERT(sizeof(unsigned long) >= sizeof(void *));
		/* create timer.
		 * FIXME grammar
		 * parameter for callback is pointer to corresponding
		 * `struct tg_timer'
		 */
		rc = c2_timer_init(&tg->tg_timers[i].tgt_timer, C2_TIMER_HARD,
				tgt->tgt_interval, tgt->tgt_repeat,
				test_timer_callback_mt, (unsigned long) tgt);
		C2_ASSERT(rc == 0);
		/* attach timer to timer group locality */
		c2_timer_attach(&tg->tg_timers[i].tgt_timer, &tg->tg_loc);
	}
	/* synchronize with all master threads */
	c2_semaphore_up(&tg->tg_sem_init);
	c2_semaphore_down(&tg->tg_sem_resume);
	/* start() all timers */
	for (i = 0; i < NR_TIMERS_TG; ++i) {
		rc = c2_timer_start(&tg->tg_timers[i].tgt_timer);
		C2_ASSERT(rc == 0);
	}
	/* wait for all timers.
	 * FIXME grammar
	 * every timer will `up' their semaphore when done
	 */
	for (i = 0; i < NR_TIMERS_TG; ++i)
		c2_semaphore_down(&tg->tg_timers[i].tgt_done);
	/* stop() all timers */
	for (i = 0; i < NR_TIMERS_TG; ++i) {
		rc = c2_timer_stop(&tg->tg_timers[i].tgt_timer);
		C2_ASSERT(rc == 0);
	}
	/* fini() all timers */
	for (i = 0; i < NR_TIMERS_TG; ++i) {
		c2_timer_fini(&tg->tg_timers[i].tgt_timer);
		c2_semaphore_fini(&tg->tg_timers[i].tgt_done);
	}
	/* resume all slaves */
	for (i = 0; i < NR_THREADS_TG; ++i)
		c2_semaphore_up(&tg->tg_slaves[i].tgs_sem_resume);
	/* fini() all slave threads */
	for (i = 0; i < NR_THREADS_TG; ++i) {
		c2_thread_join(&tg->tg_threads[i]);
		c2_thread_fini(&tg->tg_threads[i]);
	}
	/* fini() thread group locality */
	c2_timer_locality_fini(&tg->tg_loc);
	/* fini() all slave semaphores */
	for (i = 0; i < NR_THREADS_TG; ++i) {
		c2_semaphore_fini(&tg->tg_slaves[i].tgs_sem_init);
		c2_semaphore_fini(&tg->tg_slaves[i].tgs_sem_resume);
	}
	/* signal to main thread */
	c2_semaphore_up(&tg->tg_sem_done);
}

/*
 * this (main) thread		master threads		slave threads
 * start all masters
 * wait for master init
 *				init slaves
 *				wait for all slaves
 *							attach to locality
 *				barrier with slaves	barrier with master
 *				sync with main
 *				wait for all masters
 * barrier with all masters	barrier with main
 *				init all timers
 *				run all timers
 *				fini() all timers
 *				barrier with slaves	barrier with master
 *							detach from locality
 *							exit from thread
 *				wait for slaves
 *					termination
 * barrier with all masters	barrier with main
 *				exit from thread
 * wait for masters termination
 */
static void test_timer_many_timers_mt()
{
	int			   i;
	int			   rc;
	static struct thread_group tg[NR_TG];

	/* init() all semaphores */
	for (i = 0; i < NR_TG; ++i) {
		sem_init_zero(&tg[i].tg_sem_init);
		sem_init_zero(&tg[i].tg_sem_resume);
		sem_init_zero(&tg[i].tg_sem_done);
	}

	/* init RNG seeds for thread groups */
	for (i = 0; i < NR_TG; ++i)
		tg[i].tg_seed = i;

	/* start all masters from every thread group */
	for (i = 0; i < NR_TG; ++i) {
		rc = C2_THREAD_INIT(&tg[i].tg_master, struct thread_group *,
				NULL, &test_timer_master_mt,
				&tg[i], "timer test master");
		C2_ASSERT(rc == 0);
	}

	/* wait for masters initializing */
	for (i = 0; i < NR_TG; ++i)
		c2_semaphore_down(&tg[i].tg_sem_init);

	/* resume all masters */
	for (i = 0; i < NR_TG; ++i)
		c2_semaphore_up(&tg[i].tg_sem_resume);

	/* wait for finishing */
	for (i = 0; i < NR_TG; ++i)
		c2_semaphore_down(&tg[i].tg_sem_done);

	/* fini() all semaphores and master threads */
	for (i = 0; i < NR_TG; ++i) {
		c2_semaphore_fini(&tg[i].tg_sem_init);
		c2_semaphore_fini(&tg[i].tg_sem_resume);
		c2_semaphore_fini(&tg[i].tg_sem_done);

		c2_thread_join(&tg[i].tg_master);
		c2_thread_fini(&tg[i].tg_master);
	}
}

static void test_timer_hard(void)
{
	int tick;
	int timer;

	test_timer_locality_default();
	for (tick = 1; tick <= NR_TICKS; ++tick)
		for (timer = 1; timer <= NR_TIMERS; ++timer)
			test_timer_many_timers(timer, tick);
	test_timer_many_timers(1, MANY_TICKS_NR);
	test_timer_many_timers_mt();
}

void test_timer(void)
{
	// test_timer_soft();
	test_timer_hard();
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
