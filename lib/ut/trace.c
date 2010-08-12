/* -*- C -*- */

#include "lib/ub.h"
#include "lib/ut.h"
#include "lib/thread.h"
#include "lib/trace.h"
#include "lib/assert.h"

enum {
	NR       = 16,
	NR_INNER = 100000
};

void test_trace(void)
{
	int i;
	int result;
	struct c2_thread t[NR];

	C2_TRACE_POINT({ uint32_t a; }, 42);
	C2_TRACE_POINT({ uint32_t b; }, 43);
	for (i = 0; i < NR_INNER; ++i)
		C2_TRACE_POINT({ uint32_t c; uint64_t d; }, i, i*i);

	memset(t, 0, sizeof t);
	for (i = 0; i < NR; ++i) {
		result = C2_THREAD_INIT(&t[i], int, NULL,
					LAMBDA(void, (int d) {
			int j;

			for (j = 0; j < NR_INNER; ++j)
				C2_TRACE_POINT({ uint32_t c; uint64_t d; }, 
					       d, d * j); 
						}), i);
	}
	for (i = 0; i < NR; ++i) {
		c2_thread_join(&t[i]);
		c2_thread_fini(&t[i]);
	}
}

enum {
	UB_ITER = 1000000
};

static void ub_empty(int i)
{
	C2_TRACE_POINT({},);
}

static void ub_8(int i)
{
	C2_TRACE_POINT({ uint64_t x; }, i);
}

static void ub_64(int i)
{
	C2_TRACE_POINT({ uint64_t x0;
			uint64_t x1;
			uint64_t x2;
			uint64_t x3;
			uint64_t x4;
			uint64_t x5;
			uint64_t x6;
			uint64_t x7; }, i, i + 1, i + 2, i + 3, i + 4, i + 5, 
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
