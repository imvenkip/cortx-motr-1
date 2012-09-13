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

#include "lib/misc.h"		/* C2_SET0 */
#include "lib/arith.h"		/* min_check */

#include "net/test/stats.h"

/**
   @defgroup NetTestStatsInternals Statistics Collector
   @ingroup NetTestInternals

   @see
   @ref net-test

   @{
 */

void c2_net_test_stats_reset(struct c2_net_test_stats *stats)
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
	c2_uint128_mul64(&v128, value, value);
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

/**
   @} end of NetTestStatsInternals group
 */

/**
   @defgroup NetTestTimestampInternals Timestamp
   @ingroup NetTestInternals

   @{
 */

void c2_net_test_timestamp_init(struct c2_net_test_timestamp *t, uint64_t seq)
{
	C2_PRE(t != NULL);

	t->ntt_seq   = seq;
	t->ntt_magic = C2_NET_TEST_TIMESTAMP_MAGIC;
	t->ntt_time  = c2_time_now();
}

TYPE_DESCR(c2_net_test_timestamp) = {
	FIELD_DESCR(struct c2_net_test_timestamp, ntt_time),
	FIELD_DESCR(struct c2_net_test_timestamp, ntt_seq),
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

/**
   @} end of NetTestTimestampInternals group
 */

/**
   @defgroup NetTestStatsMPSInternals Messages Per Second Statistics
   @ingroup NetTestInternals

   @{
 */

void c2_net_test_mps_init(struct c2_net_test_mps *mps,
			  uint64_t messages,
			  c2_time_t timestamp,
			  c2_time_t interval)
{
	C2_PRE(mps != NULL);

	c2_net_test_stats_reset(&mps->ntmps_stats);

	mps->ntmps_last_nr	 = messages;
	mps->ntmps_last_time     = timestamp;
	mps->ntmps_time_interval = interval;
}

c2_time_t c2_net_test_mps_add(struct c2_net_test_mps *mps,
			      c2_bcount_t messages,
			      c2_time_t timestamp)
{
	c2_bcount_t   messages_delta;
	c2_time_t     time_delta;
	c2_time_t     time_next;
	uint64_t      time_delta_ns;
	unsigned long m_per_sec;
	unsigned long M_10;		/* M^10 */
	unsigned long M;

	C2_PRE(mps != NULL);
	C2_PRE(messages >= mps->ntmps_last_nr);
	C2_PRE(c2_time_after_eq(timestamp, mps->ntmps_last_time));

	messages_delta = messages - mps->ntmps_last_nr;
	time_delta  = c2_time_sub(timestamp, mps->ntmps_last_time);
	time_next   = c2_time_add(timestamp, mps->ntmps_time_interval);

	if (!c2_time_after_eq(timestamp, time_next))
		return time_next;

	mps->ntmps_last_nr = messages;
	/** @todo problem with small mps->ntmps_time_interval can be here */
	mps->ntmps_last_time  = time_next;

	time_delta_ns = c2_time_seconds(time_delta) * C2_TIME_ONE_BILLION +
			c2_time_nanoseconds(time_delta);
	/*
	   To measure bandwidth in messages/sec it needs to be calculated
	   (messages_delta / time_delta_ns) * 1'000'000'000 =
	   (messages_delta * 1'000'000'000) / time_delta_ns =
	   ((messages_delta * (10^M)) / time_delta_ns) * (10^(9-M)),
	   where M is some parameter. To perform integer division M
	   should be maximized in range [0, 9] - in case if M < 9
	   there is a loss of precision.
	 */
	if (C2_BCOUNT_MAX / C2_TIME_ONE_BILLION > messages_delta) {
		/* simple case. M = 9 */
		m_per_sec = messages_delta * C2_TIME_ONE_BILLION / time_delta_ns;
	} else {
		/* harder case. M is in range [0, 9) */
		M_10 = 1;
		for (M = 0; M < 8; ++M) {
			if (C2_BCOUNT_MAX / (M_10 * 10) > messages_delta)
				M_10 *= 10;
			else
				break;
		}
		/* M is maximized */
		m_per_sec = (messages_delta * M_10 / time_delta_ns) *
			    (C2_TIME_ONE_BILLION / M_10);
	}
	c2_net_test_stats_add(&mps->ntmps_stats, m_per_sec);

	return time_next;
}

/**
   @} end of NetTestStatsMPSInternals group
 */

/**
   @defgroup NetTestStatsMsgNRInternals Messages Per Second Statistics
   @ingroup NetTestInternals

   @{
 */

void c2_net_test_msg_nr_add(struct c2_net_test_msg_nr *msg_nr,
			    const struct c2_net_test_msg_nr *msg_nr2)
{
	C2_PRE(msg_nr != NULL);
	C2_PRE(msg_nr2 != NULL);

	msg_nr->ntmn_total  += msg_nr2->ntmn_total;
	msg_nr->ntmn_failed += msg_nr2->ntmn_failed;
	msg_nr->ntmn_bad    += msg_nr2->ntmn_bad;
}

/**
   @} end of NetTestStatsMsgNRInternals group
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
