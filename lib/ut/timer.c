/* -*- C -*- */

#include "lib/ut.h"
#include "lib/time.h"
#include "lib/timer.h"
#include "lib/assert.h"
#include "lib/thread.h"

#include <stdio.h>

static int count = 0;

unsigned long tick(unsigned long data)
{
	struct c2_time now;

	c2_time_now(&now);
	count ++;
	printf("%lu.%lu: timer1 tick = %d\n", now.ts.tv_sec, now.ts.tv_nsec, count);
	return 0;
}

unsigned long tack(unsigned long data)
{
	static int tack;
	struct c2_time now;

	c2_time_now(&now);
	tack += data;
	printf("%lu.%lu:    timer2 tack = %d\n", now.ts.tv_sec, now.ts.tv_nsec, tack);
	return 0;
}

void test_2_timers()
{
	struct c2_timer timer1, timer2;
	struct c2_time i1, i2, wait;

	c2_time_set(&i1, 2, 0);
	c2_timer_init(&timer1, &i1, 10, tick, 0);
	c2_timer_start(&timer1);

	c2_time_set(&i2, 9, 0);
	c2_timer_init(&timer2, &i2, 10, tack, 10000);
	c2_timer_start(&timer2);

	c2_time_set(&wait, 0, 500000);
	/* let's do something, e.g. just waiting */
	while (count < 10)
		c2_nanosleep(&wait, NULL);
	c2_timer_stop(&timer1);


	/* start timer1 again, and do something, e.g. just waiting */
	c2_timer_start(&timer1);
	while (count < 15)
		c2_nanosleep(&wait, NULL);
	c2_timer_stop(&timer2);
	c2_timer_fini(&timer2);

	while (count < 20)
		c2_nanosleep(&wait, NULL);

	c2_timer_stop(&timer1);
	c2_timer_fini(&timer1);
}

void timer1_thread(int unused)
{
	struct c2_timer timer1;
	struct c2_time i1, wait;

	c2_time_set(&i1, 2, 0);
	c2_timer_init(&timer1, &i1, 10, tick, 0);
	c2_timer_start(&timer1);

	c2_time_set(&wait, 0, 500000);
	/* let's do something, e.g. just waiting */
	while (count < 10)
		c2_nanosleep(&wait, NULL);

	/* stop and start timer1 again, and do something, e.g. just waiting */
	c2_timer_stop(&timer1);
	c2_timer_start(&timer1);

	while (count < 20)
		c2_nanosleep(&wait, NULL);

	c2_timer_stop(&timer1);
	c2_timer_fini(&timer1);
	printf("timer1 thread exit\n");
}

void timer2_thread(int unused)
{
	struct c2_timer timer2;
	struct c2_time  i2, wait;

	c2_time_set(&i2, 9, 0);
	c2_timer_init(&timer2, &i2, 10, tack, 10000);
	c2_timer_start(&timer2);

	while (count < 15)
		c2_nanosleep(&wait, NULL);
	c2_timer_stop(&timer2);
	c2_timer_fini(&timer2);
	printf("timer2 thread exit\n");
}

void test_timer(void)
{
	struct c2_thread t1 = { 0 };
	struct c2_thread t2 = { 0 };
	int rc;

	printf("start timer testing...\n");
	test_2_timers();
	printf("======================\n");
	printf("start 2 timers in 2 threads ...\n");
	count = 0;

	rc = C2_THREAD_INIT(&t1, int, NULL, &timer1_thread, 0);
	C2_ASSERT(rc == 0);
	rc = C2_THREAD_INIT(&t2, int, NULL, &timer2_thread, 0);
	C2_ASSERT(rc == 0);

	c2_thread_join(&t1);
	c2_thread_fini(&t1);
	c2_thread_join(&t2);
	c2_thread_fini(&t2);

	printf("end timer testing...\n");
	return;
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
