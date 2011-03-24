/* -*- C -*- */

#include "lib/ut.h"
#include "lib/time.h"
#include "lib/assert.h"

void test_time(void)
{
	struct c2_time t1, t2, t3;
	int rc;

	/* check wrapping: time "1" should be after time "very big" */
	c2_time_set(&t1, 1, 0);
	c2_time_set(&t2, (uint64_t)(-1), 0);
	C2_UT_ASSERT(c2_time_after(&t1, &t2));
	C2_UT_ASSERT(c2_time_after(&t1, &C2_TIME_NEVER));

	c2_time_now(&t1);
	t2 = t1;
	C2_UT_ASSERT(c2_time_flatten(&t1) != 0);
	C2_UT_ASSERT(c2_time_flatten(&t2) == c2_time_flatten(&t1));

	c2_time_set(&t1, 1234, 987654321);
	C2_UT_ASSERT(c2_time_flatten(&t1) == 1234987654321);

	t2 = t1;
	C2_UT_ASSERT(c2_time_after_eq(&t2, &t1));

	c2_time_set(&t2, 1235, 987654322);
	C2_UT_ASSERT(c2_time_after(&t2, &t1));

	c2_time_sub(&t2, &t1, &t3);
	C2_UT_ASSERT(c2_time_flatten(&t3) == 1000000001);

	c2_time_sub(&t1, &t2, &t3);
	C2_UT_ASSERT(c2_time_flatten(&t3) == (uint64_t)(-1000000001));

	c2_time_set(&t2, 1, 500000000);
	c2_time_add(&t1, &t2, &t3);
	C2_UT_ASSERT(c2_time_flatten(&t3) == 1236487654321);

	rc = c2_nanosleep(&t2, &t1);
	C2_UT_ASSERT(rc == 0);
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
