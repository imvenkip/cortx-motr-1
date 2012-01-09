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
 *		    Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 03/04/2011
 */

#include "lib/ut.h"		/* C2_UT_ASSERT */
#include "lib/time.h"		/* c2_time_t */
#include "lib/timer.h"		/* c2_timer */
#include "lib/assert.h"		/* C2_ASSERT */
#include "lib/thread.h"		/* C2_THREAD_INIT */
#include "lib/semaphore.h"	/* c2_semaphore */
#include "lib/memory.h"		/* C2_ALLOC_ARR */
#include "lib/atomic.h"		/* c2_atomic64 */

#include <stdlib.h>		/* rand */
#include <unistd.h>		/* syscall */
#include <sys/syscall.h>	/* syscall */

enum {
	NR_TIMERS     = 10,	/* number of timers in tests */
	NR_TG	      = 10,	/* number of thread groups */
	NR_THREADS_TG = 10,	/* number of slave threads per thread group */
	NR_TIMERS_TG  = 100,	/* number of timers per thread group */
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
	c2_time_t	     tgt_expire;
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

static pid_t		   loc_default_tid;
static struct c2_semaphore loc_default_lock;

static pid_t		    test_locality_tid;
static struct c2_semaphore *test_locality_lock;

static struct c2_atomic64 callbacks_executed;

static pid_t gettid()
{
	return syscall(SYS_gettid);
}

static c2_time_t make_time(int ms)
{
	c2_time_t t;

	c2_time_set(&t, ms / 1000, (ms % 1000) * 1000000);
	return t;
}

static c2_time_t make_time_abs(int ms)
{
	return c2_time_add(c2_time_now(), make_time(ms));
}

static int time_rand_ms(int min_ms, int max_ms)
{
	return min_ms + (rand() * 1. / RAND_MAX) * (max_ms - min_ms);
}

static void sem_init_zero(struct c2_semaphore *sem)
{
	int rc = c2_semaphore_init(sem, 0);
	C2_ASSERT(rc == 0);
}

static unsigned long timer_callback(unsigned long data)
{
	c2_atomic64_inc(&callbacks_executed);
	return 0;
}

/**
   Test timers.

   @param timer_type timer type
   @param nr_timers number of timers in this test
   @param interval_min_ms minimum value for timer interval
   @param interval_max_ms maximum value for timer interval
	  for every timer it will be chosen with rand()
	  in range [interval_min_ms, interval_max_ms]
   @param wait_time_ms function will wait this time and then c2_time_stop()
	  for all timers
   @param callbacks_min @see callbacks_max
   @param callbacks_max number of executed callbacks should be
	  in the interval [callbacks_min, callbacks_max]
	  (this is checked with C2_UT_ASSERT())
 */
static void test_timers(enum c2_timer_type timer_type, int nr_timers,
		int interval_min_ms, int interval_max_ms,
		int wait_time_ms, int callbacks_min, int callbacks_max)
{
	struct c2_timer *timers;
	int		 i;
	int		 time;
	int		 rc;
	c2_time_t	 zero_time;
	c2_time_t	 wait;
	c2_time_t	 rem;

	c2_atomic64_set(&callbacks_executed, 0);
	srand(0);
	C2_ALLOC_ARR(timers, nr_timers);

	/* c2_timer_init() */
	for (i = 0; i < nr_timers; ++i) {
		time = time_rand_ms(interval_min_ms, interval_max_ms);
		rc = c2_timer_init(&timers[i], timer_type, 
				make_time_abs(time),
				timer_callback,
				i);
		C2_ASSERT(rc == 0);
	}
	/* c2_timer_start() */
	for (i = 0; i < nr_timers; ++i) {
		rc = c2_timer_start(&timers[i]);
		C2_ASSERT(rc == 0);
	}
	/* wait some time */
	c2_time_set(&zero_time, 0, 0);
	wait = make_time(wait_time_ms);
	do
		c2_nanosleep(wait, &rem);
	while ((wait = rem) != zero_time);
	/* c2_timer_stop() */
	for (i = 0; i < nr_timers; ++i) {
		rc = c2_timer_stop(&timers[i]);
		C2_ASSERT(rc == 0);
	}
	/* c2_timer_fini() */
	for (i = 0; i < nr_timers; ++i) {
		rc = c2_timer_fini(&timers[i]);
		C2_UT_ASSERT(rc == 0);
	}

	c2_free(timers);

	C2_UT_ASSERT(c2_atomic64_get(&callbacks_executed) >= callbacks_min);
	C2_UT_ASSERT(c2_atomic64_get(&callbacks_executed) <= callbacks_max);
}

static unsigned long locality_default_callback(unsigned long data)
{
	C2_UT_ASSERT(gettid() == loc_default_tid);
	c2_semaphore_up(&loc_default_lock);
	return 0;
}

static void timer_locality_default_test()
{
	int		rc;
	struct c2_timer timer;

	rc = c2_timer_init(&timer, C2_TIMER_HARD, make_time_abs(100),
			&locality_default_callback, 0);
	C2_ASSERT(rc == 0);

	sem_init_zero(&loc_default_lock);

	loc_default_tid = gettid();
	rc = c2_timer_start(&timer);
	C2_ASSERT(rc == 0);
	c2_semaphore_down(&loc_default_lock);

	rc = c2_timer_stop(&timer);
	C2_ASSERT(rc == 0);

	c2_semaphore_fini(&loc_default_lock);
	rc = c2_timer_fini(&timer);
	C2_UT_ASSERT(rc == 0);
}

static unsigned long locality_test_callback(unsigned long data)
{
	C2_ASSERT(data >= 0);
	C2_ASSERT(test_locality_tid == gettid());
	c2_semaphore_up(&test_locality_lock[data]);
	return 0;
}

static void timer_locality_test(int nr_timers,
		int interval_min_ms, int interval_max_ms)
{
	int                      i;
	int                      rc;
	struct c2_timer		*timers;
	struct c2_timer_locality loc;
	int			 time;

	C2_ALLOC_ARR(timers, nr_timers);
	C2_ALLOC_ARR(test_locality_lock, nr_timers);

	test_locality_tid = gettid();
	for (i = 0; i < nr_timers; ++i)
		sem_init_zero(&test_locality_lock[i]);

	c2_timer_locality_init(&loc);
	rc = c2_timer_thread_attach(&loc);
	C2_ASSERT(rc == 0);

	/* c2_timer_init() */
	for (i = 0; i < nr_timers; ++i) {
		time = time_rand_ms(interval_min_ms, interval_max_ms);
		rc = c2_timer_init(&timers[i], C2_TIMER_HARD,
				make_time_abs(time),
				&locality_test_callback, i);
		C2_ASSERT(rc == 0);
		rc = c2_timer_attach(&timers[i], &loc);
		C2_ASSERT(rc == 0);
	}

	/* c2_timer_start() */
	for (i = 0; i < nr_timers; ++i) {
		rc = c2_timer_start(&timers[i]);
		C2_ASSERT(rc == 0);
	}

	for (i = 0; i < nr_timers; ++i)
		c2_semaphore_down(&test_locality_lock[i]);

	/* c2_timer_stop(), c2_timer_fini() */
	for (i = 0; i < nr_timers; ++i) {
		rc = c2_timer_stop(&timers[i]);
		C2_ASSERT(rc == 0);
		rc = c2_timer_fini(&timers[i]);
		C2_UT_ASSERT(rc == 0);
		c2_semaphore_fini(&test_locality_lock[i]);
	}

	c2_timer_thread_detach(&loc);
	c2_timer_locality_fini(&loc);

	c2_free(test_locality_lock);
	c2_free(timers);
}

/*
   It isn't safe to use C2_UT_ASSERT() in signal handler code,
   therefore instead of C2_UT_ASSERT() was used C2_ASSERT().
 */
static unsigned long test_timer_callback_mt(unsigned long data)
{
	struct tg_timer *tgt = (struct tg_timer *)data;
	bool		 found = false;
	pid_t		 tid = gettid();
	int		 i;

	C2_ASSERT(tgt != NULL);
	/* check thread ID */
	for (i = 0; i < NR_THREADS_TG; ++i)
		if (tgt->tgt_group->tg_slaves[i].tgs_tid == tid) {
			found = true;
			break;
		}
	C2_ASSERT(found);
	/* callback is done */
	c2_semaphore_up(&tgt->tgt_done);
	return 0;
}

static void test_timer_slave_mt(struct tg_slave *slave)
{
	int rc;

	slave->tgs_tid = gettid();
	/* add slave thread to locality */
	rc = c2_timer_thread_attach(&slave->tgs_group->tg_loc);
	C2_ASSERT(rc == 0);
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
		/* expiration time is in [now + 1, now + 100] ms range */
		c2_time_set(&tgt->tgt_expire, 0,
				(1 + rand_r(&tg->tg_seed) % 100) * 1000000);
		tgt->tgt_expire = c2_time_add(c2_time_now(), tgt->tgt_expire);
		/* init timer semaphore */
		sem_init_zero(&tgt->tgt_done);
		/* `unsigned long' must have enough space to contain `void*' */
		C2_CASSERT(sizeof(unsigned long) >= sizeof(void *));
		/*
		 * create timer.
		 * parameter for callback is pointer to corresponding
		 * `struct tg_timer'
		 */
		rc = c2_timer_init(&tg->tg_timers[i].tgt_timer, C2_TIMER_HARD,
				tgt->tgt_expire,
				test_timer_callback_mt, (unsigned long) tgt);
		C2_ASSERT(rc == 0);
		/* attach timer to timer group locality */
		rc = c2_timer_attach(&tg->tg_timers[i].tgt_timer, &tg->tg_loc);
		C2_ASSERT(rc == 0);
	}
	/* synchronize with all master threads */
	c2_semaphore_up(&tg->tg_sem_init);
	c2_semaphore_down(&tg->tg_sem_resume);
	/* start() all timers */
	for (i = 0; i < NR_TIMERS_TG; ++i) {
		rc = c2_timer_start(&tg->tg_timers[i].tgt_timer);
		C2_ASSERT(rc == 0);
	}
	/* wait for all timers */
	for (i = 0; i < NR_TIMERS_TG; ++i)
		c2_semaphore_down(&tg->tg_timers[i].tgt_done);
	/* stop() all timers */
	for (i = 0; i < NR_TIMERS_TG; ++i) {
		rc = c2_timer_stop(&tg->tg_timers[i].tgt_timer);
		C2_ASSERT(rc == 0);
	}
	/* fini() all timers */
	for (i = 0; i < NR_TIMERS_TG; ++i) {
		rc = c2_timer_fini(&tg->tg_timers[i].tgt_timer);
		C2_ASSERT(rc == 0);
		c2_semaphore_fini(&tg->tg_timers[i].tgt_done);
	}
	/* resume all slaves */
	for (i = 0; i < NR_THREADS_TG; ++i)
		c2_semaphore_up(&tg->tg_slaves[i].tgs_sem_resume);
	/* fini() all slave threads */
	for (i = 0; i < NR_THREADS_TG; ++i) {
		rc = c2_thread_join(&tg->tg_threads[i]);
		C2_ASSERT(rc == 0);
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

/**
   @verbatim
   this (main) thread		master threads		slave threads
   start all masters
   wait for masters init
				init slaves
				wait for all slaves
							attach to locality
				barrier with slaves	barrier with master
				sync with main
				wait for all masters
   barrier with all masters	barrier with main
				init all timers
				run all timers
				fini() all timers
				barrier with slaves	barrier with master
							detach from locality
							exit from thread
				wait for slaves
					termination
   barrier with all masters	barrier with main
				exit from thread
   wait for masters termination
   @endverbatim
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

		rc = c2_thread_join(&tg[i].tg_master);
		C2_ASSERT(rc == 0);
		c2_thread_fini(&tg[i].tg_master);
	}
}

void test_timer(void)
{
	int		   i;
	int		   j;
	enum c2_timer_type timer_types[2] = {C2_TIMER_SOFT, C2_TIMER_HARD};

	/* soft and hard timers tests */
	for (j = 0; j < 2; ++j)
		for (i = 1; i < NR_TIMERS; ++i) {
			/* simple test */
			test_timers(timer_types[j], i, 0, 10, 5, 0, i);
			/* zero-time test */
			test_timers(timer_types[j], i, 0, 0, 50, i, i);
			/* cancel-timer-before-callback-executed test */
			test_timers(timer_types[j], i, 10000, 10000, 50, 0, 0);
		}
	/* simple hard timer default locality test */
	timer_locality_default_test();
	/* test many hard timers in locality */
	for (i = 1; i < NR_TIMERS; ++i)
		timer_locality_test(i, 10, 30);
	/* hard timer test in multithreaded environment */
	test_timer_many_timers_mt();
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
