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
 * Original creation date: 12/10/2010
 */

#include "lib/cdefs.h"	/* C2_EXPORTED */
#include "lib/assert.h" /* C2_PRE */
#include "lib/time.h"

/**
   @addtogroup time

   Implementation of time functions on top of all c2_time_t defs

   @{
*/

c2_time_t c2_time(uint64_t secs, long ns)
{
	c2_time_t t;
	c2_time_set(&t, secs, ns);
	return t;
}

c2_time_t c2_time_set(c2_time_t *time, uint64_t secs, long ns)
{
	*time = secs * C2_TIME_ONE_BILLION + ns;
	return *time;
}
C2_EXPORTED(c2_time_set);

c2_time_t c2_time_add(const c2_time_t t1, const c2_time_t t2)
{
	c2_time_t res;

	C2_PRE(C2_TIME_NEVER >= t1);
	C2_PRE(C2_TIME_NEVER >= t2);

	if (t1 == C2_TIME_NEVER || t2 == C2_TIME_NEVER)
		res = C2_TIME_NEVER;
	else
		res = t1 + t2;

	C2_POST(res >= t1);
	C2_POST(res >= t2);
	return res;
}
C2_EXPORTED(c2_time_add);

c2_time_t c2_time_sub(const c2_time_t t1, const c2_time_t t2)
{
	c2_time_t res;
	C2_PRE(C2_TIME_NEVER >= t1);
	C2_PRE(t2 < C2_TIME_NEVER);
	C2_PRE(t1 >= t2);

	if (t1 == C2_TIME_NEVER)
		res = C2_TIME_NEVER;
	else
		res = t1 - t2;

	C2_POST(t1 >= res);
	return res;
}

uint64_t c2_time_seconds(const c2_time_t time)
{
	return time / C2_TIME_ONE_BILLION;
}
C2_EXPORTED(c2_time_seconds);

uint64_t c2_time_nanoseconds(const c2_time_t time)
{

        return time % C2_TIME_ONE_BILLION;
}
C2_EXPORTED(c2_time_nanoseconds);

c2_time_t c2_time_from_now(uint64_t secs, long ns)
{
	return c2_time_now() + secs * C2_TIME_ONE_BILLION + ns;
}
C2_EXPORTED(c2_time_from_now);

const c2_time_t C2_TIME_NEVER = ~0ULL;
C2_EXPORTED(C2_TIME_NEVER);

/** @} end of time group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
