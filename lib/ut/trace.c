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
 * Original creation date: 08/12/2010
 */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/ub.h"
#include "lib/ut.h"
#include "lib/thread.h"
#include "lib/assert.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_UT
#include "lib/trace.h"

enum {
	NR       = 16,
	NR_INNER = 100000
};

static struct c2_thread t[NR];

void test_trace(void)
{
	int i;
	int result;
	uint64_t u64;

	C2_LOG("forty two: %i", 42);
	C2_LOG("forty three and tree: %i %llu", 43, (unsigned long long)(u64 = 3));
	for (i = 0; i < NR_INNER; ++i)
		C2_LOG("c: %i, d: %i", i, i*i);

	C2_SET_ARR0(t);
	for (i = 0; i < NR; ++i) {
		result = C2_THREAD_INIT(&t[i], int, NULL,
					LAMBDA(void, (int d) {
			int j;

			for (j = 0; j < NR_INNER; ++j)
				C2_LOG("d: %i, d*j: %i", d, d * j);
						}), i, "test_trace_%i", i);
		C2_ASSERT(result == 0);
	}
	for (i = 0; i < NR; ++i) {
		c2_thread_join(&t[i]);
		c2_thread_fini(&t[i]);
	}
	C2_LOG("X: %i and Y: %i", 43, result + 1);
	C2_LOG("%llx char: %c %llx string: %s",
		0x1234567887654321ULL,
		'c',
		0xfefefefefefefefeULL,
		(char *)"foobar");
}

enum {
	UB_ITER = 5000000
};

static void ub_empty(int i)
{
	C2_LOG("msg");
}

static void ub_8(int i)
{
	C2_LOG("%i", i);
}

static void ub_64(int i)
{
	C2_LOG("%i %i %i %i %i %i %i %i",
		i, i + 1, i + 2, i + 3, i + 4, i + 5,
		i + 6, i + 7);
}

struct c2_ub_set c2_trace_ub = {
	.us_name = "trace-ub",
	.us_run  = {
		{ .ut_name = "empty",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_empty },

		{ .ut_name = "8",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_8 },

		{ .ut_name = "64",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_64 },

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
