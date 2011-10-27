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

unsigned long hard_tick(unsigned long data)
{
	c2_time_t now;

	printf("hard tick %lu, thread %ld, ", data, pthread_self());
	now = c2_time_now();
	printf("time %lu.%lu\n",
		c2_time_seconds(now), c2_time_nanoseconds(now));
	return 0;
}

void test_hard_1()
{
	struct c2_timer timer1;
	c2_time_t       i1;
	c2_time_t       wait;
	int i;

	c2_time_set(&i1, 1, 0);
	C2_UT_ASSERT(!c2_timer_init(&timer1, C2_TIMER_HARD, i1, 20, hard_tick, 0));
	c2_timer_start(&timer1);

	c2_time_set(&wait, 0, 500000000); /* 0.5 second */
	for (i = 0; i < 9; ++i)
		c2_nanosleep(wait, NULL);

	c2_timer_stop(&timer1);
	c2_timer_fini(&timer1);
}

void test_timer_hard(void)
{
	// struct c2_thread t1 = { 0 };
	// int rc;

	printf("start hard timer testing....\n");
	test_hard_1();
}


pid_t gettid()
{
	return syscall(SYS_gettid);
}

struct c2_mutex tswitch;
c2_time_t one_sec;
c2_time_t infinity_time;
static int tcount = 0;	// XXX not thread-safe
pid_t tid[0x10];
timer_t timer_id;
int signal_tid;

#define PRINT_THREAD(...) printf("TID = %d, ", gettid());	printf(__VA_ARGS__);

void tc_sighandler(int param)
{
	PRINT_THREAD("sighandler called.\n");
	C2_ASSERT(gettid() == signal_tid);
}

void tc_thread(int param)
{
	struct sigaction sa;
	int index = tcount++;
	struct sigevent se;
	struct itimerspec it;

	tid[index] = gettid();
	PRINT_THREAD("started.\n");

	if (index == 0) {
		PRINT_THREAD("installing SIGRTMIN handler...\n");
		sa.sa_handler = tc_sighandler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = SA_NODEFER;
		sigaction(SIGRTMIN, &sa, NULL);
	}

	c2_nanosleep(one_sec, NULL);
	if (index == 0) {
		signal_tid = tid[rand() % 3];

		PRINT_THREAD("installing timer +.5 sec\n");
		se.sigev_notify = SIGEV_THREAD_ID;
		se.sigev_signo = SIGRTMIN;
		se._sigev_un._tid = signal_tid;
		PRINT_THREAD("installing target thread as %d\n", signal_tid);
		C2_ASSERT(timer_create(CLOCK_REALTIME, &se, &timer_id) == 0);
		it.it_value.tv_sec = 0;
		it.it_value.tv_nsec = 500000000;
		it.it_interval.tv_sec = 100;
		it.it_interval.tv_nsec = 0;
		C2_ASSERT(timer_settime(timer_id, 0, &it, NULL) == 0);
	}

	PRINT_THREAD("start sleeping...\n");
	c2_nanosleep(one_sec, NULL);
	PRINT_THREAD("end sleeping.\n");
}

void test_timer_create()
{
	struct c2_thread t1 = { 0 };
	struct c2_thread t2 = { 0 };
	int rc;

	tcount = 0;
	tid[2] = gettid();
	c2_time_set(&infinity_time, 10000, 0);
	c2_time_set(&one_sec, 1, 0);
	rc = C2_THREAD_INIT(&t1, int, NULL, &tc_thread, 0, "tc_thread1");
	C2_ASSERT(rc == 0);
	rc = C2_THREAD_INIT(&t2, int, NULL, &tc_thread, 0, "tc_thread2");
	C2_ASSERT(rc == 0);


	c2_thread_join(&t1);
	c2_thread_fini(&t1);
	c2_thread_join(&t2);
	c2_thread_fini(&t2);
	C2_ASSERT(timer_delete(timer_id) == 0);
}

void test_timer(void)
{
	int i;
	// test_timer_soft();
	// test_timer_hard();
	for (i = 0; i < 0x100; ++i)
		test_timer_create();
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
