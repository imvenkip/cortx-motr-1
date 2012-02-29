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
 * Original creation date: 05/23/2010
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <time.h>    /* nanosleep */
#include <stdlib.h>
#include <string.h>
#include <pthread.h> /* barrier */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/ut.h"
#include "lib/thread.h"
#include "lib/atomic.h"
#include "lib/assert.h"

#ifdef HAVE_PTHREAD_BARRIER_T
enum {
	NR = 64
};

static struct c2_atomic64 atom;
static pthread_barrier_t bar[NR];
static pthread_barrier_t let[NR];

static void wait(pthread_barrier_t *b)
{
	int result;

	result = pthread_barrier_wait(b);
	C2_UT_ASSERT(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
}

static void worker(int id)
{
	int i;
	int j;
	int k;

	struct timespec delay;

	for (i = 0; i < NR; ++i) {
		wait(&let[i]);
		for (j = 0; j < 2; ++j) {
			for (k = 0; k < NR; ++k) {
				if ((id + i + j) % 2 == 0)
					c2_atomic64_sub(&atom, id);
				else
					c2_atomic64_add(&atom, id);
			}
		}
		delay.tv_sec = 0;
		delay.tv_nsec = (((id + i) % 4) + 1) * 1000;
		nanosleep(&delay, NULL);
		wait(&bar[i]);
		C2_UT_ASSERT(c2_atomic64_get(&atom) == 0);
	}
}

struct el {
	struct el *next;
	int        datum;
};

static struct el *list;

static void cas_insert(struct el *e)
{
	do
		e->next = list;
	while (!C2_ATOMIC64_CAS(&list, e->next, e));
}

static void cas_delete(void)
{
	struct el *e;

	do
		e = list;
	while (!C2_ATOMIC64_CAS(&list, e, e->next));
}

static void breset(pthread_barrier_t *b, int n)
{
	int result;

	result = pthread_barrier_destroy(b);
	C2_UT_ASSERT(result == 0);
	result = pthread_barrier_init(b, NULL, n);
	C2_UT_ASSERT(result == 0);
}

static void cas(int id)
{
	int i;
	int j;

	struct el e = {
		.next  = NULL,
		.datum = id
	};
	struct el d[NR];

	wait(&bar[0]);
	/* and all together now: non-blocking list insertion. */
	for (i = 0; i < NR; ++i) {
		cas_insert(&e);
		wait(&bar[1]);
		wait(&bar[2]);
	}
	for (i = 0; i < NR; ++i) {
		for (j = 0; j < NR; ++j)
			cas_insert(&d[j]);
		for (j = 0; j < NR; ++j)
			cas_delete();
		wait(&bar[3]);
		wait(&bar[4]);
	}
}
#endif

void test_atomic(void)
{
#ifdef HAVE_PTHREAD_BARRIER_T
	int               i;
	int               j;
	int               result;
	uint64_t          sum;
	uint64_t          sum1;
	bool              zero;
	struct c2_thread  t[NR];
	struct el        *e;

	C2_SET_ARR0(t);
	c2_atomic64_set(&atom, 0);
	sum = 0;
	for (i = 0; i < NR; ++i) {
		c2_atomic64_add(&atom, i);
		sum += i;
		C2_UT_ASSERT(c2_atomic64_get(&atom) == sum);
	}

	for (i = sum; i > 0; --i) {
		zero = c2_atomic64_dec_and_test(&atom);
		C2_UT_ASSERT(zero == (i == 1));
	}

	for (i = 0; i < ARRAY_SIZE(bar); ++i) {
		result = pthread_barrier_init(&bar[i], NULL, NR + 1);
		C2_UT_ASSERT(result == 0);
		result = pthread_barrier_init(&let[i], NULL, NR + 1);
		C2_UT_ASSERT(result == 0);
	}

	c2_atomic64_set(&atom, 0);

	for (i = 0; i < ARRAY_SIZE(t); ++i) {
		result = C2_THREAD_INIT(&t[i], int, NULL, &worker, i, "worker");
		C2_UT_ASSERT(result == 0);
	}

	for (i = 0; i < NR; ++i) {
		wait(&let[i]);
		wait(&bar[i]);
		C2_UT_ASSERT(c2_atomic64_get(&atom) == 0);
	}

	for (i = 0; i < ARRAY_SIZE(t); ++i) {
		c2_thread_join(&t[i]);
		c2_thread_fini(&t[i]);
	}

	/*
	 * c2_atomic64_cas() test.
	 */

	C2_CASSERT(ARRAY_SIZE(bar) > 5);

	/* bar[0] is reached when all cas() threads are created. */
	breset(&bar[0], NR);
	/* bar[1] is reached when every cas() thread insert its element in the
	   global lock-free linked list. */
	breset(&bar[1], NR + 1);
	/* bar[2] is reached when main thread checked that the list is correct
	   after the previous concurrent step and reset the list to NULL. */
	breset(&bar[2], NR + 1);
	/* bar[3] is reached after every cas() step inserted NR elements in the
	   global lock-free list and then removed NR entries from it. */
	breset(&bar[3], NR + 1);
	/* bar[4] is reached after main thread checked that the list is empty
	   after the previous step. */
	breset(&bar[4], NR + 1);

	for (i = 0; i < ARRAY_SIZE(t); ++i) {
		result = C2_THREAD_INIT(&t[i], int, NULL, &cas, i, "cas");
		C2_UT_ASSERT(result == 0);
	}

	for (j = 0; j < NR; ++j) {
		wait(&bar[1]);
		breset(&bar[1], NR + 1);

		/* all threads inserted their identifiers in the list, check. */
		for (i = 0, sum1 = 0, e = list; i < NR; ++i, e = e->next) {
			C2_UT_ASSERT(e != NULL);
			C2_UT_ASSERT(0 <= e->datum && e->datum < NR);
			sum1 += e->datum;
		}
		C2_UT_ASSERT(sum == sum1);
		list = NULL;
		wait(&bar[2]);
		breset(&bar[2], NR + 1);
	}
	for (j = 0; j < NR; ++j) {
		wait(&bar[3]);
		breset(&bar[3], NR + 1);
		C2_UT_ASSERT(list == NULL);
		wait(&bar[4]);
		breset(&bar[4], NR + 1);
	}

	for (i = 0; i < ARRAY_SIZE(bar); ++i) {
		result = pthread_barrier_destroy(&bar[i]);
		C2_UT_ASSERT(result == 0);
		result = pthread_barrier_destroy(&let[i]);
		C2_UT_ASSERT(result == 0);
	}

	for (i = 0; i < ARRAY_SIZE(t); ++i) {
		c2_thread_join(&t[i]);
		c2_thread_fini(&t[i]);
	}
#else
        C2_UT_ASSERT("pthread barriers are not supported!" == NULL);
#endif

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
