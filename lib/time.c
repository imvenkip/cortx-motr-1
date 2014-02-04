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

#include "lib/time.h"
#include "lib/misc.h"  /* M0_EXPORTED */

/**
   @addtogroup time

   Implementation of time functions on top of all m0_time_t defs

   @{
*/

m0_time_t m0_time(uint64_t secs, long ns)
{
	return M0_MKTIME(secs, ns);
}
M0_EXPORTED(m0_time);

m0_time_t m0_time_add(const m0_time_t t1, const m0_time_t t2)
{
	m0_time_t res;

	M0_PRE(M0_TIME_NEVER >= t1);
	M0_PRE(M0_TIME_NEVER >= t2);

	if (t1 == M0_TIME_NEVER || t2 == M0_TIME_NEVER)
		res = M0_TIME_NEVER;
	else
		res = t1 + t2;

	M0_POST(res >= t1);
	M0_POST(res >= t2);
	return res;
}
M0_EXPORTED(m0_time_add);

m0_time_t m0_time_sub(const m0_time_t t1, const m0_time_t t2)
{
	m0_time_t res;
	M0_PRE(M0_TIME_NEVER >= t1);
	M0_PRE(t2 < M0_TIME_NEVER);
	//M0_PRE(t1 >= t2);

	if (t1 == M0_TIME_NEVER)
		res = M0_TIME_NEVER;
	else
		res = t1 - t2;

	//M0_POST(t1 >= res);
	return res;
}
M0_EXPORTED(m0_time_sub);

uint64_t m0_time_seconds(const m0_time_t time)
{
	return time / M0_TIME_ONE_SECOND;
}
M0_EXPORTED(m0_time_seconds);

uint64_t m0_time_nanoseconds(const m0_time_t time)
{

        return time % M0_TIME_ONE_SECOND;
}
M0_EXPORTED(m0_time_nanoseconds);

m0_time_t m0_time_from_now(uint64_t secs, long ns)
{
	return m0_time_now() + m0_time(secs, ns);
}
M0_EXPORTED(m0_time_from_now);

bool m0_time_is_in_past(m0_time_t t)
{
	return t < m0_time_now();
}

const m0_time_t M0_TIME_IMMEDIATELY = 0;
const m0_time_t M0_TIME_NEVER       = ~0ULL;
M0_EXPORTED(M0_TIME_IMMEDIATELY);
M0_EXPORTED(M0_TIME_NEVER);

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
