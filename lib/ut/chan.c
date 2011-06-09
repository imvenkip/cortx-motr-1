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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 05/13/2010
 */

#include "lib/ut.h"
#include "lib/thread.h"
#include "lib/mutex.h"
#include "lib/chan.h"
#include "lib/assert.h"
#include "lib/timer.h"
#include "lib/cdefs.h"     /* C2_EXPORTED */

enum {
	NR = 16
};

static struct c2_thread t[NR];
static struct c2_chan   c[NR];
static struct c2_clink  l[NR];

static void t0(int self)
{
	int i;
	int j;

	for (i = 0; i < NR; ++i) {
		for (j = 0; j < NR; ++j) {
			if (j != self)
				c2_chan_signal(&c[j]);
		}

		for (j = 0; j < NR - 1; ++j)
			c2_chan_wait(&l[self]);
	}
}

static int flag;

static void cb1(struct c2_clink *clink)
{
	flag += 1;
}

static void cb2(struct c2_clink *clink)
{
	flag += 2;
}

unsigned long signal_the_chan_in_timer(unsigned long data)
{
	struct c2_chan *chan = (struct c2_chan *)data;
	c2_chan_signal(chan);
	return 0;
}

void test_chan(void)
{
	struct c2_chan  chan;
	struct c2_clink clink1;
	struct c2_clink clink2;
	c2_time_t       now;
	c2_time_t       delta;
	c2_time_t       expire;
	struct c2_timer timer;
	int i;
	bool got;

	c2_chan_init(&chan);

	/* test call-back notification */
	flag = 0;
	c2_clink_init(&clink1, &cb1);
	c2_clink_add(&chan, &clink1);
	c2_chan_signal(&chan);
	C2_UT_ASSERT(flag == 1);
	c2_chan_broadcast(&chan);
	C2_UT_ASSERT(flag == 2);

	c2_clink_init(&clink2, &cb2);
	c2_clink_add(&chan, &clink2);

	flag = 0;
	c2_chan_signal(&chan);
	C2_UT_ASSERT(flag == 1 || flag == 2);
	flag = 0;
	c2_chan_broadcast(&chan);
	C2_UT_ASSERT(flag == 3);

	c2_clink_del(&clink1);

	flag = 0;
	c2_chan_signal(&chan);
	C2_UT_ASSERT(flag == 2);
	flag = 0;
	c2_chan_broadcast(&chan);
	C2_UT_ASSERT(flag == 2);

	c2_clink_del(&clink2);

	c2_clink_fini(&clink1);
	c2_clink_fini(&clink2);

	/* test synchronous notification */

	c2_clink_init(&clink1, NULL);
	c2_clink_add(&chan, &clink1);

	got = c2_chan_trywait(&clink1);
	C2_UT_ASSERT(!got);

	c2_chan_signal(&chan);
	got = c2_chan_trywait(&clink1);
	C2_UT_ASSERT(got);

	c2_chan_signal(&chan);
	c2_chan_wait(&clink1);

	/* wait will expire after 2 seconds */
	c2_time_set(&delta, 2, 0);
	expire = c2_time_add(c2_time_now(&now), delta);
	got = c2_chan_timedwait(&clink1, expire); /* wait 2 seconds */
	C2_UT_ASSERT(!got);

	/* chan is signaled after 1 second. so the wait will return true */
	c2_time_set(&expire, 1, 0);
	c2_timer_init(&timer, C2_TIMER_SOFT, expire, 1,
		      &signal_the_chan_in_timer, (unsigned long)&chan);
	c2_timer_start(&timer);
	expire = c2_time_add(c2_time_now(&now), delta);
	got = c2_chan_timedwait(&clink1, expire); /* wait 2 seconds */
	C2_UT_ASSERT(got);
	c2_timer_stop(&timer);
	c2_timer_fini(&timer);

	/* chan is signaled after 3 seconds. so the wait will timeout and
	   return false. Another wait should work.*/
	c2_time_set(&expire, 3, 0);
	c2_timer_init(&timer, C2_TIMER_SOFT, expire, 1,
		      &signal_the_chan_in_timer, (unsigned long)&chan);
	c2_timer_start(&timer);
	expire = c2_time_add(c2_time_now(&now), delta);
	got = c2_chan_timedwait(&clink1, expire); /* wait 2 seconds */
	C2_UT_ASSERT(!got);
	c2_chan_wait(&clink1); /* another wait. Timer will signal in 1 second */
	c2_timer_stop(&timer);
	c2_timer_fini(&timer);

	c2_clink_del(&clink1);
	c2_clink_fini(&clink1);

	c2_chan_fini(&chan);

	/* multi-threaded test */

	for (i = 0; i < ARRAY_SIZE(c); ++i) {
		c2_chan_init(&c[i]);
		c2_clink_init(&l[i], NULL);
		c2_clink_add(&c[i], &l[i]);
	}

	for (i = 0; i < ARRAY_SIZE(t); ++i) {
		got = C2_THREAD_INIT(&t[i], int, NULL, &t0, i, "t0");
		C2_UT_ASSERT(got == 0);
	}

	for (i = 0; i < ARRAY_SIZE(t); ++i) {
		c2_thread_join(&t[i]);
		c2_thread_fini(&t[i]);
	}

	for (i = 0; i < ARRAY_SIZE(c); ++i) {
		c2_clink_del(&l[i]);
		c2_clink_fini(&l[i]);
		c2_chan_fini(&c[i]);
	}
}
C2_EXPORTED(test_chan);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
