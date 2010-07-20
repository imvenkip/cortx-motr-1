/* -*- C -*- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/ut.h"
#include "lib/thread.h"
#include "lib/mutex.h"
#include "lib/chan.h"
#include "lib/assert.h"

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

void test_chan(void)
{
	struct c2_chan  chan;
	struct c2_clink clink1;
	struct c2_clink clink2;
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
		got = C2_THREAD_INIT(&t[i], int, NULL, &t0, i);
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

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
