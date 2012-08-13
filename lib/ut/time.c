/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nathan Rutman <Nathan_Rutman@xyratex.com>,
 *                  Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 12/06/2010
 */

#include "lib/ut.h"
#include "lib/time.h"
#include "lib/assert.h"

void test_time(void)
{
	c2_time_t t1, t2, t3;
	int rc;

	c2_time_set(&t1, 1, 0);
	c2_time_set(&t2, 2, 0);
	C2_UT_ASSERT(t2 > t1);
	C2_UT_ASSERT(C2_TIME_NEVER > t1);
	C2_UT_ASSERT(t2 < C2_TIME_NEVER);

	c2_time_set(&t1, 1234, 0);
	C2_UT_ASSERT(C2_TIME_NEVER > t1);

	t1 = c2_time_now();
	t2 = t1;
	C2_UT_ASSERT(t1 != 0);

	c2_time_set(&t1, 1234, 987654321);
	C2_UT_ASSERT(t1 == 1234987654321);

	t2 = t1;
	C2_UT_ASSERT(t2 == t1);

	c2_time_set(&t2, 1235, 987654322);
	C2_UT_ASSERT(t2 > t1);

	t3 = c2_time_sub(t2, t1);
	C2_UT_ASSERT(t3 == 1000000001);

	c2_time_set(&t2, 1, 500000000);
	t3 = c2_time_add(t1, t2);
	C2_UT_ASSERT(t3 == 1236487654321);

	c2_time_set(&t2, 0, C2_TIME_ONE_BILLION/100);
	rc = c2_nanosleep(t2, &t1);
	C2_UT_ASSERT(rc == 0);

	c2_time_set(&t1, 1234, 987654321);
	c2_time_set(&t2, 1, 500000000);
	t2 = c2_time_add(t1, C2_TIME_NEVER);
	C2_UT_ASSERT(t2 == C2_TIME_NEVER);

	c2_time_set(&t2, 1, 500000000);
	t2 = c2_time_add(C2_TIME_NEVER, t1);
	C2_UT_ASSERT(t2 == C2_TIME_NEVER);

	c2_time_set(&t2, 1, 500000000);
	t2 = c2_time_sub(C2_TIME_NEVER, t1);
	C2_UT_ASSERT(t2 == C2_TIME_NEVER);
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
