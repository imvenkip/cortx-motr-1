/* -*- C -*- */

#include <time.h>    /* nanosleep */
#include <stdio.h>   /* printf */
#include <stdlib.h>
#include <string.h>
#include <pthread.h> /* barrier */

#include "lib/thread.h"
#include "lib/atomic.h"
#include "lib/assert.h"

enum {
	NR = 255
};

static struct c2_atomic64 atom;
static pthread_barrier_t bar[NR];
static pthread_barrier_t let[NR];

static void wait(pthread_barrier_t *b)
{
	int result;

	result = pthread_barrier_wait(b);
	C2_ASSERT(result == 0 || result == PTHREAD_BARRIER_SERIAL_THREAD);
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
		C2_ASSERT(c2_atomic64_get(&atom) == 0);
	}
}

void test_atomic(void)
{
	int              i;
	int              result;
	uint64_t         sum;
	bool             zero;
	struct c2_thread t[NR];

	memset(t, 0, sizeof t);
	c2_atomic64_set(&atom, 0);
	sum = 0;
	for (i = 0; i < NR; ++i) {
		c2_atomic64_add(&atom, i);
		sum += i;
		C2_ASSERT(c2_atomic64_get(&atom) == sum);
	}

	for (i = sum; i > 0; --i) {
		zero = c2_atomic64_dec_and_test(&atom);
		C2_ASSERT(zero == (i == 1));
	}

	for (i = 0; i < ARRAY_SIZE(bar); ++i) {
		result = pthread_barrier_init(&bar[i], NULL, NR + 1);
		C2_ASSERT(result == 0);
		result = pthread_barrier_init(&let[i], NULL, NR + 1);
		C2_ASSERT(result == 0);
	}

	c2_atomic64_set(&atom, 0);

	for (i = 0; i < ARRAY_SIZE(t); ++i) {
		result = C2_THREAD_INIT(&t[i], int, &worker, i);
		C2_ASSERT(result == 0);
	}

	for (i = 0; i < NR; ++i) {
		wait(&let[i]);
		wait(&bar[i]);
		C2_ASSERT(c2_atomic64_get(&atom) == 0);
		printf(".");
	}

	for (i = 0; i < ARRAY_SIZE(t); ++i) {
		c2_thread_join(&t[i]);
		c2_thread_fini(&t[i]);
	}

	for (i = 0; i < ARRAY_SIZE(bar); ++i) {
		result = pthread_barrier_destroy(&bar[i]);
		C2_ASSERT(result == 0);
		result = pthread_barrier_destroy(&let[i]);
		C2_ASSERT(result == 0);
	}
	printf("\n");
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
