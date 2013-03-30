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

#include "ut/ut.h"
#include "lib/time.h"
#include "lib/assert.h"

void test_time(void)
{
	m0_time_t t1, t2, t3;
	int rc;

	t1 = m0_time(1, 0);
	t2 = m0_time(2, 0);
	M0_UT_ASSERT(t2 > t1);
	M0_UT_ASSERT(M0_TIME_NEVER > t1);
	M0_UT_ASSERT(t2 < M0_TIME_NEVER);

	t1 = m0_time(1234, 0);
	M0_UT_ASSERT(M0_TIME_NEVER > t1);

	t1 = m0_time_now();
	t2 = t1;
	M0_UT_ASSERT(t1 != 0);

	t1 = m0_time(1234, 987654321);
	M0_UT_ASSERT(t1 == 1234987654321);

	t2 = t1;
	M0_UT_ASSERT(t2 == t1);

	t2 = m0_time(1235, 987654322);
	M0_UT_ASSERT(t2 > t1);

	t3 = m0_time_sub(t2, t1);
	M0_UT_ASSERT(t3 == 1000000001);

	t2 = m0_time(1, 500000000);
	t3 = m0_time_add(t1, t2);
	M0_UT_ASSERT(t3 == 1236487654321);

	t2 = m0_time(0, M0_TIME_ONE_BILLION / 100);
	rc = m0_nanosleep(t2, &t1);
	M0_UT_ASSERT(rc == 0);

	t1 = m0_time(1234, 987654321);
	t2 = m0_time_add(t1, M0_TIME_NEVER);
	M0_UT_ASSERT(t2 == M0_TIME_NEVER);

	t2 = m0_time(1, 500000000);
	t2 = m0_time_add(M0_TIME_NEVER, t1);
	M0_UT_ASSERT(t2 == M0_TIME_NEVER);

	t2 = m0_time_sub(M0_TIME_NEVER, t1);
	M0_UT_ASSERT(t2 == M0_TIME_NEVER);
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
