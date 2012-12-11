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
 * Original creation date: 05/30/2010
 */

#include "lib/misc.h"  /* M0_SET_ARR0 */
#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/thread.h"
#include "lib/bitmap.h"
#include "lib/assert.h"

enum {
	NR = 255
};

void test_is_awkward(void);

static int t0place;

static void t0(int x)
{
	t0place = x;
}

static struct m0_thread t[NR];
static int r[NR];

static void t2(int n)
{
	int result;

	if (n > 0) {
		result = M0_THREAD_INIT(&t[n - 1], int, NULL, &t2, n - 1, "t2");
		M0_UT_ASSERT(result == 0);
	}
	r[n] = n;
}

static void t3(int n)
{
	int result;
	struct m0_bitmap t3bm;
	struct m0_thread_handle myhandle;

	/* set affinity (confine) to CPU 0 */
	M0_UT_ASSERT(m0_bitmap_init(&t3bm, 3) == 0);
	m0_bitmap_set(&t3bm, 0, true);

	result = m0_thread_confine(&t[n], &t3bm);
	M0_UT_ASSERT(result == 0);

	m0_bitmap_fini(&t3bm);

	/* another handle test */
	m0_thread_self(&myhandle);
	M0_UT_ASSERT(m0_thread_handle_eq(&myhandle, &t[n].t_h));
}

static char t1place[100];

static void forty_two_func(const char *s)
{
	strcpy(t1place, s);
}

static int lambda42_init(int x)
{
	return 0;
}

static void lambda42_func(int x)
{
}

static int lambda_42_init(int x)
{
	return -42;
}

static void lambda_42_func(int x)
{
}

void test_thread(void)
{
	int i;
	int result;
	struct m0_thread_handle thandle;
	struct m0_thread_handle myhandle;

	m0_thread_self(&myhandle);
	m0_thread_self(&thandle);
	M0_UT_ASSERT(m0_thread_handle_eq(&myhandle, &thandle));

	M0_SET_ARR0(r);
	t0place = 0;
	result = M0_THREAD_INIT(&t[0], int, NULL, &t0, 42, "t0");
	M0_UT_ASSERT(result == 0);

	M0_UT_ASSERT(!m0_thread_handle_eq(&myhandle, &t[0].t_h));
	M0_UT_ASSERT(m0_thread_handle_eq(&t[0].t_h, &t[0].t_h));

	M0_UT_ASSERT(m0_thread_join(&t[0]) == 0);
	m0_thread_fini(&t[0]);
	M0_UT_ASSERT(t0place == 42);

	result = M0_THREAD_INIT(&t[0], const char *, NULL, &forty_two_func,
				(const char *)"forty-two", "fourty-two");
	M0_UT_ASSERT(result == 0);
	m0_thread_join(&t[0]);
	m0_thread_fini(&t[0]);
	M0_UT_ASSERT(!strcmp(t1place, "forty-two"));

	t2(NR - 1);
	for (i = NR - 2; i >= 0; --i) {
		/* this loop is safe, because t[n] fills t[n - 1] before
		   exiting. */
		m0_thread_join(&t[i]);
		m0_thread_fini(&t[i]);
		M0_UT_ASSERT(r[i] == i);
	}

	/* test init functions */
	result = M0_THREAD_INIT(&t[0], int, &lambda42_init, &lambda42_func,
				42, "lambda42");
	M0_UT_ASSERT(result == 0);
	m0_thread_join(&t[0]);
	m0_thread_fini(&t[0]);

	result = M0_THREAD_INIT(&t[0], int, &lambda_42_init, &lambda_42_func,
				42, "lambda-42");
	M0_UT_ASSERT(result == -42);
	m0_thread_fini(&t[0]);

	/* test confine */
	result = M0_THREAD_INIT(&t[0], int, NULL, &t3, 0, "t3");
	M0_UT_ASSERT(result == 0);
	m0_thread_join(&t[0]);
	m0_thread_fini(&t[0]);

	/* test m0_is_awkward() */
	test_is_awkward();
}
M0_EXPORTED(test_thread);

enum {
	UB_ITER = 10000
};

static struct m0_thread ubt[UB_ITER];

static void ub_init(void)
{
	M0_SET_ARR0(ubt);
}

static void ub_fini(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ubt); ++i)
		m0_thread_fini(&ubt[i]);
}

static void ub0(int x)
{
}

static void ub_spawn(int i)
{
	int result;
	result = M0_THREAD_INIT(&ubt[i], int, NULL, &ub0, 0, "ub0");
	M0_ASSERT(result == 0);
}

static void ub_join(int i)
{
	m0_thread_join(&ubt[i]);
}

static int ub_spawn_initcall(int x)
{
	return 0;
}

static void ub_spawn_init(int i)
{
	int result;
	result = M0_THREAD_INIT(&ubt[i], int, &ub_spawn_initcall, &ub0, 0,
				"ub0");
	M0_ASSERT(result == 0);
}

static void ub_join_all(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ubt); ++i)
		m0_thread_join(&ubt[i]);
	ub_init();
}

struct m0_ub_set m0_thread_ub = {
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

static void set_and_check_is_awkward(void)
{
	m0_enter_awkward();
	M0_UT_ASSERT(m0_is_awkward() == true);

	m0_exit_awkward();
	M0_UT_ASSERT(m0_is_awkward() == false);
}

static void ut_t0_handler1(int arg)
{
	/* check default */
	M0_UT_ASSERT(m0_is_awkward() == false);

	/* set and check is_awkward() */
	set_and_check_is_awkward();
}

void test_is_awkward(void)
{
	int result;

	result = M0_THREAD_INIT(&t[0], int, NULL, &ut_t0_handler1,
				0, "ut_t0_handler1");
	M0_UT_ASSERT(result == 0);

	m0_thread_join(&t[0]);
	m0_thread_fini(&t[0]);
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
