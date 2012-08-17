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
 * Original creation date: 03/22/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifndef __KERNEL__
#include <math.h>	/* sqrt */
#endif

#include "lib/misc.h"	/* C2_SET0 */
#include "lib/arith.h"	/* min_check */

#include "net/test/stats.h"

/**
   @defgroup NetTestStatsInternals Statistics Collector
   @ingroup NetTestInternals

   @see
   @ref net-test

   @{
 */

void c2_net_test_stats_init(struct c2_net_test_stats *stats)
{
	C2_SET0(stats);
	C2_POST(c2_net_test_stats_invariant(stats));
}

bool c2_net_test_stats_invariant(const struct c2_net_test_stats *stats)
{
	if (stats == NULL)
		return false;
	if (stats->nts_count == 0) {
	       if (stats->nts_min != 0 || stats->nts_max != 0 ||
		   !c2_net_test_uint256_is_eq(&stats->nts_sum, 0) ||
		   !c2_net_test_uint256_is_eq(&stats->nts_sum_sqr, 0))
		       return false;
	}
	return true;
}

void c2_net_test_stats_add(struct c2_net_test_stats *stats,
			   unsigned long value)
{
	C2_PRE(c2_net_test_stats_invariant(stats));

	stats->nts_count++;
	if (stats->nts_count == 1) {
		stats->nts_min = value;
		stats->nts_max = value;
	} else {
		stats->nts_min = min_check(stats->nts_min, value);
		stats->nts_max = max_check(stats->nts_max, value);
	}
	c2_net_test_uint256_add(&stats->nts_sum, value);
	c2_net_test_uint256_add_sqr(&stats->nts_sum_sqr, value);
}

void c2_net_test_stats_add_stats(struct c2_net_test_stats *stats,
				 const struct c2_net_test_stats *stats2)
{
	C2_PRE(c2_net_test_stats_invariant(stats));
	C2_PRE(c2_net_test_stats_invariant(stats2));

	if (stats->nts_count == 0) {
		stats->nts_min = stats2->nts_min;
		stats->nts_max = stats2->nts_max;
	} else if (stats2->nts_count != 0) {
		stats->nts_min = min_check(stats->nts_min, stats2->nts_min);
		stats->nts_max = max_check(stats->nts_max, stats2->nts_max);
	}
	stats->nts_count += stats2->nts_count;
	c2_net_test_uint256_add_uint256(&stats->nts_sum, &stats2->nts_sum);
	c2_net_test_uint256_add_uint256(&stats->nts_sum_sqr,
					&stats2->nts_sum_sqr);
}

unsigned long c2_net_test_stats_count(const struct c2_net_test_stats *stats)
{
	C2_PRE(c2_net_test_stats_invariant(stats));

	return stats->nts_count;
}

unsigned long c2_net_test_stats_min(const struct c2_net_test_stats *stats)
{
	C2_PRE(c2_net_test_stats_invariant(stats));

	return stats->nts_min;
}

unsigned long c2_net_test_stats_max(const struct c2_net_test_stats *stats)
{
	C2_PRE(c2_net_test_stats_invariant(stats));

	return stats->nts_max;
}

#ifndef __KERNEL__
double c2_net_test_stats_avg(const struct c2_net_test_stats *stats)
{
	double sum;

	C2_PRE(c2_net_test_stats_invariant(stats));

	sum = c2_net_test_uint256_double_get(&stats->nts_sum);
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
	sum_sqr	= c2_net_test_uint256_double_get(&stats->nts_sum_sqr);
	stddev	= (sum_sqr - N * mean * mean) / (N - 1.);
	stddev  = stddev < 0. ? 0. : stddev;
	stddev	= sqrt(stddev);
	return stddev;
}
#endif

TYPE_DESCR(c2_net_test_stats) = {
	FIELD_DESCR(struct c2_net_test_stats, nts_count),
	FIELD_DESCR(struct c2_net_test_stats, nts_min),
	FIELD_DESCR(struct c2_net_test_stats, nts_max),
};

c2_bcount_t c2_net_test_stats_serialize(enum c2_net_test_serialize_op op,
					struct c2_net_test_stats *stats,
					struct c2_bufvec *bv,
					c2_bcount_t bv_offset)
{
	struct c2_net_test_uint256 *p_uint256;
	c2_bcount_t		    len_total;
	c2_bcount_t		    len;
	int			    i;

	len = c2_net_test_serialize(op, stats,
				    USE_TYPE_DESCR(c2_net_test_stats),
				    bv, bv_offset);
	len_total = len;
	for (i = 0; i < 2; ++i) {
		if (len != 0) {
			p_uint256 = stats == NULL ? NULL : i == 0 ?
				    &stats->nts_sum : &stats->nts_sum_sqr;
			len = c2_net_test_uint256_serialize(op, p_uint256, bv,
							bv_offset + len_total);
			len_total += len;
		}
	}
	return len == 0 ? 0 : len_total;
}

static c2_time_t unsigned_long2c2_time_t(unsigned long value)
{
	c2_time_t time;

	return c2_time_set(&time, value / C2_TIME_ONE_BILLION,
				  value % C2_TIME_ONE_BILLION);
}

static unsigned long c2_time_t2unsigned_long(c2_time_t time)
{
	return c2_time_seconds(time) * C2_TIME_ONE_BILLION +
	       c2_time_nanoseconds(time);
}

void c2_net_test_stats_time_add(struct c2_net_test_stats *stats,
				c2_time_t time)
{
	c2_net_test_stats_add(stats, c2_time_t2unsigned_long(time));
}

c2_time_t c2_net_test_stats_time_min(struct c2_net_test_stats *stats)
{
	return unsigned_long2c2_time_t(c2_net_test_stats_min(stats));
}

c2_time_t c2_net_test_stats_time_max(struct c2_net_test_stats *stats)
{
	return unsigned_long2c2_time_t(c2_net_test_stats_max(stats));
}

#ifndef __KERNEL__
static c2_time_t double2c2_time_t(double value)
{
	c2_time_t time;
	uint64_t  seconds;
	uint64_t  nanoseconds;

	seconds	    = (uint64_t) floor(value / C2_TIME_ONE_BILLION);
	nanoseconds = (uint64_t) (value - seconds * C2_TIME_ONE_BILLION);
	return c2_time_set(&time, seconds, nanoseconds);
}

c2_time_t c2_net_test_stats_time_avg(struct c2_net_test_stats *stats)
{
	return double2c2_time_t(c2_net_test_stats_avg(stats));
}

c2_time_t c2_net_test_stats_time_stddev(struct c2_net_test_stats *stats)
{
	return double2c2_time_t(c2_net_test_stats_stddev(stats));
}
#endif

void c2_net_test_timestamp_init(struct c2_net_test_timestamp *t)
{
	C2_PRE(t != NULL);

	t->ntt_magic = C2_NET_TEST_TIMESTAMP_MAGIC;
	t->ntt_time  = c2_time_now();
}

TYPE_DESCR(c2_net_test_timestamp) = {
	FIELD_DESCR(struct c2_net_test_timestamp, ntt_time),
	FIELD_DESCR(struct c2_net_test_timestamp, ntt_magic),
};

c2_bcount_t c2_net_test_timestamp_serialize(enum c2_net_test_serialize_op op,
					    struct c2_net_test_timestamp *t,
					    struct c2_bufvec *bv,
					    c2_bcount_t bv_offset)
{
	c2_bcount_t len;

	C2_PRE(ergo(op == C2_NET_TEST_DESERIALIZE, t != NULL));

	len = c2_net_test_serialize(op, t,
				    USE_TYPE_DESCR(c2_net_test_timestamp),
				    bv, bv_offset);
	return op == C2_NET_TEST_DESERIALIZE ?
	       t->ntt_magic == C2_NET_TEST_TIMESTAMP_MAGIC ? len : 0 : len;
}

c2_time_t c2_net_test_timestamp_get(struct c2_net_test_timestamp *t)
{
	C2_PRE(t != NULL);

	return t->ntt_time;
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
