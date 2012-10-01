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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 08/18/2012
 */

#include <math.h>	/* sqrt */
#include "lib/types.h"	/* UINT64_MAX */
#include "lib/assert.h"	/* C2_PRE */

#include "net/test/stats.h"

/**
   @addtogroup NetTestStatsInternals

   @see
   @ref net-test

   @{
 */

static double double_get(const struct c2_uint128 *v128)
{
	return v128->u_lo * 1. + v128->u_hi * (UINT64_MAX + 1.);
}

double c2_net_test_stats_sum(const struct c2_net_test_stats *stats)
{
	C2_PRE(c2_net_test_stats_invariant(stats));

	return double_get(&stats->nts_sum);
}

double c2_net_test_stats_avg(const struct c2_net_test_stats *stats)
{
	double sum;

	C2_PRE(c2_net_test_stats_invariant(stats));

	sum = double_get(&stats->nts_sum);
	return stats->nts_count == 0 ? 0. : sum / stats->nts_count;
}

double c2_net_test_stats_stddev(const struct c2_net_test_stats *stats)
{
	double mean;
	double stddev;
	double N;
	double sum_sqr;

	C2_PRE(c2_net_test_stats_invariant(stats));

	if (stats->nts_count == 0 || stats->nts_count == 1)
		return 0.;

	mean	= c2_net_test_stats_avg(stats);
	N	= stats->nts_count;
	sum_sqr	= double_get(&stats->nts_sum_sqr);
	stddev	= (sum_sqr - N * mean * mean) / (N - 1.);
	stddev  = stddev < 0. ? 0. : stddev;
	stddev	= sqrt(stddev);
	return stddev;
}

static c2_time_t double2c2_time_t(double value)
{
	uint64_t seconds;
	uint64_t nanoseconds;

	seconds	    = (uint64_t) floor(value / C2_TIME_ONE_BILLION);
	nanoseconds = (uint64_t) (value - seconds * C2_TIME_ONE_BILLION);
	return C2_MKTIME(seconds, nanoseconds);
}

c2_time_t c2_net_test_stats_time_sum(struct c2_net_test_stats *stats)
{
	return double2c2_time_t(c2_net_test_stats_sum(stats));
}

c2_time_t c2_net_test_stats_time_avg(struct c2_net_test_stats *stats)
{
	return double2c2_time_t(c2_net_test_stats_avg(stats));
}

c2_time_t c2_net_test_stats_time_stddev(struct c2_net_test_stats *stats)
{
	return double2c2_time_t(c2_net_test_stats_stddev(stats));
}

/**
   @} end of NetTestStatsInternals group
 */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
