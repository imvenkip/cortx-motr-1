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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/syscall.h>

static int count = 0;
static int verbose = 0;

static volatile bool oneshot_finished;
static volatile unsigned long oneshot_data;
static volatile int oneshot_iterations;
static volatile pid_t oneshot_tid;

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

pid_t gettid()
{
	return syscall(SYS_gettid);
}

static void test_timer_locality()
{
	struct c2_timer_locality loc;
	int loc_count;
	
	C2_UT_ASSERT(c2_timer_locality_max() > 0);
	loc_count = c2_timer_locality_count();
	C2_UT_ASSERT(loc_count < c2_timer_locality_max());
	C2_UT_ASSERT(c2_timer_locality_init(&loc) == 0);
	C2_UT_ASSERT(c2_timer_locality_count() == loc_count + 1);
	c2_timer_locality_fini(&loc);
	C2_UT_ASSERT(c2_timer_locality_count() == loc_count);
}

static unsigned long oneshot_callback(unsigned long data)
{
	oneshot_data = data;
	oneshot_tid = gettid();
	oneshot_iterations++;
	oneshot_finished = true;
	return 0;
}

static void test_timer_oneshot()
{
	struct c2_timer timer;
	struct c2_timer_locality loc;
	int rc;
	c2_time_t one_ms;
	const int data = 42;

	C2_UT_ASSERT(c2_timer_locality_init(&loc) == 0);
	c2_time_set(&one_ms, 0, 1000000);
	rc = c2_timer_init(&timer, C2_TIMER_HARD, one_ms,
			1, &oneshot_callback, 42);
	C2_ASSERT(rc == 0);
	c2_timer_attach(&timer, &loc);

	oneshot_finished = false;
	oneshot_data = ~data;
	oneshot_tid = 0;
	oneshot_iterations = 0;
	c2_timer_start(&timer);
	while (!oneshot_finished)
		;
	c2_timer_stop(&timer);
	C2_UT_ASSERT(oneshot_data == 42);
	C2_UT_ASSERT(oneshot_tid == gettid());
	C2_UT_ASSERT(oneshot_iterations == 1);
	
	c2_timer_fini(&timer);
	c2_timer_locality_fini(&loc);
}

void test_timer_hard(void)
{
	test_timer_locality();
	test_timer_oneshot();
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
