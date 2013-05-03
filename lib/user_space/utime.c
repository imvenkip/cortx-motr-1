/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 *		    Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 01/27/2011
 */

#include "lib/time.h"	/* m0_time_t */

#include "lib/assert.h" /* M0_ASSERT */
#include "lib/cdefs.h"  /* M0_EXPORTED */
#include "lib/misc.h"	/* M0_IN */
#include "lib/errno.h"	/* ENOSYS */

#include <sys/time.h>	/* gettimeofday */
#include <time.h>	/* clock_gettime */

/**
   @addtogroup time

   Implementation of m0_time_t.

   Time functions can use different clock sources.
   @see M0_CLOCK_SOURCE

   @{
*/

const enum CLOCK_SOURCES M0_CLOCK_SOURCE = M0_CLOCK_SOURCE_REALTIME_MONOTONIC;
m0_time_t		 m0_time_monotonic_offset;

static m0_time_t clock_gettime_wrapper(clockid_t clock_id)
{
	struct timespec tp;
	int		rc;

	rc = clock_gettime(clock_id, &tp);
	/** clock_gettime() can fail iff clock_id is invalid */
	M0_ASSERT(rc == 0);
	return M0_MKTIME(tp.tv_sec, tp.tv_nsec);
}

M0_INTERNAL int m0_utime_init(void)
{
	m0_time_t realtime;
	m0_time_t monotonic;

	if (M0_CLOCK_SOURCE == M0_CLOCK_SOURCE_REALTIME_MONOTONIC) {
		monotonic = clock_gettime_wrapper(CLOCK_MONOTONIC);
		realtime  = clock_gettime_wrapper(CLOCK_REALTIME);
		m0_time_monotonic_offset = realtime - monotonic;
		if (m0_time_monotonic_offset == 0)
			m0_time_monotonic_offset = 1;
	}
	return 0;
}

M0_INTERNAL void m0_utime_fini(void)
{
}

m0_time_t m0_time_now(void)
{
	struct timeval tv;
	int	       rc;
	m0_time_t      result;

	switch (M0_CLOCK_SOURCE) {
	case M0_CLOCK_SOURCE_REALTIME_MONOTONIC:
		M0_PRE(m0_time_monotonic_offset != 0);
		result = clock_gettime_wrapper(CLOCK_MONOTONIC) +
			 m0_time_monotonic_offset;
		break;
	case M0_CLOCK_SOURCE_GTOD:
		rc = gettimeofday(&tv, NULL);
		M0_ASSERT(rc == 0);
		result = M0_MKTIME(tv.tv_sec, tv.tv_usec * 1000);
		break;
	case M0_CLOCK_SOURCE_REALTIME:
	case M0_CLOCK_SOURCE_MONOTONIC:
	case M0_CLOCK_SOURCE_MONOTONIC_RAW:
		result = clock_gettime_wrapper(M0_CLOCK_SOURCE);
		break;
	default:
		M0_IMPOSSIBLE("Unknown clock source");
		result = M0_TIME_NEVER;
	};
	return result;
}
M0_EXPORTED(m0_time_now);

M0_INTERNAL m0_time_t m0_time_to_realtime(m0_time_t abs_time)
{
	m0_time_t source_time;
	m0_time_t realtime;
	m0_time_t monotonic;

	if (abs_time != M0_TIME_NEVER) {
		switch (M0_CLOCK_SOURCE) {
		case M0_CLOCK_SOURCE_MONOTONIC:
		case M0_CLOCK_SOURCE_MONOTONIC_RAW:
			source_time = clock_gettime_wrapper(M0_CLOCK_SOURCE);
			realtime    = clock_gettime_wrapper(CLOCK_REALTIME);
			abs_time   += realtime - source_time;
			break;
		case M0_CLOCK_SOURCE_REALTIME_MONOTONIC:
			monotonic = clock_gettime_wrapper(CLOCK_MONOTONIC);
			realtime  = clock_gettime_wrapper(CLOCK_REALTIME);
			/* get monotonic time */
			abs_time -= m0_time_monotonic_offset;
			/* add offset for realtime */
			abs_time += realtime - monotonic;
			/* It will mitigate time jumps between call
			 * to m0_time_now() and call to this function. */
			break;
		case M0_CLOCK_SOURCE_GTOD:
		case M0_CLOCK_SOURCE_REALTIME:
			break;
		default:
			M0_IMPOSSIBLE("Unknown clock source");
			abs_time = 0;
		}
	}
	return abs_time;
}

/** Sleep for requested time */
int m0_nanosleep(const m0_time_t req, m0_time_t *rem)
{
	struct timespec	reqts = {
		.tv_sec  = m0_time_seconds(req),
		.tv_nsec = m0_time_nanoseconds(req)
	};
	struct timespec remts;
	int		rc;

	rc = nanosleep(&reqts, &remts);
	if (rem != NULL)
		*rem = rc != 0 ? m0_time(remts.tv_sec, remts.tv_nsec) : 0;
	return rc;
}
M0_EXPORTED(m0_nanosleep);


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
