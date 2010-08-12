/* -*- C -*- */

#include "lib/ut.h"
#include "lib/thread.h"
#include "lib/mutex.h"
#include "lib/assert.h"

enum {
	NR = 255
};

static int counter;
static struct c2_thread t[NR];
static struct c2_mutex  m[NR];

static void t0(int n)
{
	int i;

	for (i = 0; i < NR; ++i) {
		c2_mutex_lock(&m[0]);
		counter += n;
		c2_mutex_unlock(&m[0]);
	}
}

static void t1(int n)
{
	int i;
	int j;

	for (i = 0; i < NR; ++i) {
		for (j = 0; j < NR; ++j)
			c2_mutex_lock(&m[j]);
		counter += n;
		for (j = 0; j < NR; ++j)
			c2_mutex_unlock(&m[j]);
	}
}

void test_mutex(void)
{
	int i;
	int sum;
	int result;

	counter = 0;

	for (sum = i = 0; i < NR; ++i) {
		c2_mutex_init(&m[i]);
		result = C2_THREAD_INIT(&t[i], int, NULL, &t0, i);
		C2_UT_ASSERT(result == 0);
		sum += i;
	}

	for (i = 0; i < NR; ++i) {
		c2_thread_join(&t[i]);
		c2_thread_fini(&t[i]);
	}

	C2_UT_ASSERT(counter == sum * NR);

	counter = 0;

	for (sum = i = 0; i < NR; ++i) {
		result = C2_THREAD_INIT(&t[i], int, NULL, &t1, i);
		C2_UT_ASSERT(result == 0);
		sum += i;
	}

	for (i = 0; i < NR; ++i) {
		c2_thread_join(&t[i]);
		c2_thread_fini(&t[i]);
	}

	for (i = 0; i < NR; ++i)
		c2_mutex_fini(&m[i]);

	C2_UT_ASSERT(counter == sum * NR);
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
