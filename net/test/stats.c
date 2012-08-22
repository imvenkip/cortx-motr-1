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
	static const struct c2_uint128 zero = {
		.u_hi = 0,
		.u_lo = 0
	};

	if (stats == NULL)
		return false;
	if (stats->nts_count == 0 &&
	    (stats->nts_min != 0 || stats->nts_max != 0 ||
	     !c2_uint128_eq(&stats->nts_sum, &zero) ||
	     !c2_uint128_eq(&stats->nts_sum_sqr, &zero)))
	       return false;
	return true;
}

void c2_net_test_stats_add(struct c2_net_test_stats *stats,
			   unsigned long value)
{
	struct c2_uint128 v128;

	C2_PRE(c2_net_test_stats_invariant(stats));

	stats->nts_count++;
	if (stats->nts_count == 1) {
		stats->nts_min = value;
		stats->nts_max = value;
	} else {
		stats->nts_min = min_check(stats->nts_min, value);
		stats->nts_max = max_check(stats->nts_max, value);
	}
	C2_CASSERT(sizeof value <= sizeof stats->nts_sum.u_hi);
	c2_uint128_add(&stats->nts_sum, stats->nts_sum,
		       C2_UINT128(0, value));
	c2_uint128_mul(&v128, value, value);
	c2_uint128_add(&stats->nts_sum_sqr, stats->nts_sum_sqr, v128);
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
	c2_uint128_add(&stats->nts_sum, stats->nts_sum, stats2->nts_sum);
	c2_uint128_add(&stats->nts_sum_sqr, stats->nts_sum_sqr,
		       stats2->nts_sum_sqr);
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

TYPE_DESCR(c2_net_test_stats) = {
	FIELD_DESCR(struct c2_net_test_stats, nts_count),
	FIELD_DESCR(struct c2_net_test_stats, nts_min),
	FIELD_DESCR(struct c2_net_test_stats, nts_max),
};

TYPE_DESCR(c2_uint128) = {
	FIELD_DESCR(struct c2_uint128, u_hi),
	FIELD_DESCR(struct c2_uint128, u_lo),
};

c2_bcount_t c2_net_test_stats_serialize(enum c2_net_test_serialize_op op,
					struct c2_net_test_stats *stats,
					struct c2_bufvec *bv,
					c2_bcount_t bv_offset)
{
	struct c2_uint128 *pv128;
	c2_bcount_t	   len_total;
	c2_bcount_t	   len;
	int		   i;

	len = c2_net_test_serialize(op, stats,
				    USE_TYPE_DESCR(c2_net_test_stats),
				    bv, bv_offset);
	len_total = len;
	for (i = 0; i < 2; ++i) {
		if (len != 0) {
			pv128 = stats == NULL ? NULL : i == 0 ?
				&stats->nts_sum : &stats->nts_sum_sqr;
			len = c2_net_test_serialize(op, pv128,
						    USE_TYPE_DESCR(c2_uint128),
						    bv, bv_offset + len_total);
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
