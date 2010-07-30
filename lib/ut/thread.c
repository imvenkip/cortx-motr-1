/* -*- C -*- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/thread.h"
#include "lib/assert.h"

enum {
	NR = 255
};

static int t0place;

static void t0(int x)
{
	t0place = x;
}

static struct c2_thread t[NR];
static int r[NR];

static void t2(int n)
{
	int result;

	if (n > 0) {
		result = C2_THREAD_INIT(&t[n - 1], int, NULL, &t2, n - 1);
		C2_UT_ASSERT(result == 0);
	}
	r[n] = n;
}

void test_thread(void)
{
	int i;
	int result;
	char t1place[100];

	memset(r, 0, sizeof r);
	t0place = 0;
	result = C2_THREAD_INIT(&t[0], int, NULL, &t0, 42);
	C2_UT_ASSERT(result == 0);
	c2_thread_join(&t[0]);
	c2_thread_fini(&t[0]);
	C2_UT_ASSERT(t0place == 42);

	result = C2_THREAD_INIT(&t[0], const char *, NULL, 
				LAMBDA(void, (const char *s) { 
						strcpy(t1place, s); } ), 
				(const char *)"forty-two");
	C2_UT_ASSERT(result == 0);
	c2_thread_join(&t[0]);
	c2_thread_fini(&t[0]);
	C2_UT_ASSERT(!strcmp(t1place, "forty-two"));

	t2(NR - 1);
	for (i = NR - 2; i >= 0; --i) {
		/* this loop is safe, because t[n] fills t[n - 1] before
		   exiting. */
		c2_thread_join(&t[i]);
		c2_thread_fini(&t[i]);
		C2_UT_ASSERT(r[i] == i);
	}

	/* test init functions */
	result = C2_THREAD_INIT(&t[0], int, 
				LAMBDA(int, (int x) { return 0; } ),
				LAMBDA(void, (int x) { ; } ), 42);
	C2_UT_ASSERT(result == 0);
	c2_thread_join(&t[0]);
	c2_thread_fini(&t[0]);

	result = C2_THREAD_INIT(&t[0], int, 
				LAMBDA(int, (int x) { return -42; } ),
				LAMBDA(void, (int x) { ; } ), 42);
	C2_UT_ASSERT(result == -42);
	c2_thread_fini(&t[0]);
}

enum {
	UB_ITER = 1000
};

static struct c2_thread ubt[UB_ITER];

static void ub_init(void)
{
	memset(ubt, 0, sizeof ubt);
}

static void ub_fini(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ubt); ++i)
		c2_thread_fini(&ubt[i]);
}

static void ub0(int x)
{
}

static void ub_spawn(int i)
{
	int result;
	result = C2_THREAD_INIT(&ubt[i], int, NULL, &ub0, 0);
	C2_ASSERT(result == 0);
}

static void ub_join(int i)
{
	c2_thread_join(&ubt[i]);
}

static int ub_spawn_initcall(int x)
{
	return 0;
}

static void ub_spawn_init(int i)
{
	int result;
	result = C2_THREAD_INIT(&ubt[i], int, &ub_spawn_initcall, &ub0, 0);
	C2_ASSERT(result == 0);
}

static void ub_join_all(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ubt); ++i)
		c2_thread_join(&ubt[i]);
	ub_init();
}

struct c2_ub_set c2_thread_ub = {
	.us_name = "thread-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = { 
		{ .ut_name  = "spawn", 
		  .ut_iter  = UB_ITER, 
		  .ut_round = ub_spawn },

		{ .ut_name  = "join", 
		  .ut_iter  = UB_ITER, 
		  .ut_round = ub_join,
		  .ut_fini  = ub_init /* sic */ },

		{ .ut_name  = "spawn-init", 
		  .ut_iter  = UB_ITER, 
		  .ut_round = ub_spawn_init,
		  .ut_fini  = ub_join_all },

		{ .ut_name = NULL }
	}
};


/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
