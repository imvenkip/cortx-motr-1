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
 * Original creation date: 07/05/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/** @todo create lib/limits.h */
#ifndef __KERNEL__
#include <limits.h>		/* ULONG_MAX */
#else
#include <linux/kernel.h>	/* ULONG_MAX */
#endif

#include "lib/cdefs.h"		/* ARRAY_SIZE */
#include "lib/misc.h"		/* C2_SET0 */
#include "lib/ut.h"		/* C2_UT_ASSERT */

#include "net/test/stats.h"

enum {
	STATS_ONE_MILLION = 1000000,
	STATS_BUF_LEN	  = 0x100,
	STATS_BUF_OFFSET  = 42,
	TIMESTAMP_BUF_LEN = 0x100,
	TIMESTAMP_SEQ	  = 123456,
};

struct stats_expected {
	unsigned long se_count;
	unsigned long se_min;
	unsigned long se_max;
#ifndef __KERNEL__
	double	      se_sum;
	double	      se_avg;
	double	      se_stddev;
#else
	unsigned      se_sum:1;
	unsigned      se_avg:1;
	unsigned      se_stddev:1;
#endif
};

#ifndef __KERNEL__
static bool is_in_eps_neighborhood(double a, double b)
{
	const double eps = 1E-5;

	return a * (1. - eps) <= b && b <= a * (1. + eps);
}
#endif

static void sample_check(struct c2_net_test_stats *stats,
			 struct stats_expected *expected)
{
	unsigned long ul;
#ifndef __KERNEL__
	double	      d;
#endif

	C2_PRE(stats	!= NULL);
	C2_PRE(expected != NULL);

	C2_UT_ASSERT(stats->nts_count == expected->se_count);

	ul = c2_net_test_stats_min(stats);
	C2_UT_ASSERT(ul == expected->se_min);

	ul = c2_net_test_stats_max(stats);
	C2_UT_ASSERT(ul == expected->se_max);

#ifndef __KERNEL__
	d = c2_net_test_stats_sum(stats);
	C2_UT_ASSERT(is_in_eps_neighborhood(expected->se_sum, d));

	d = c2_net_test_stats_avg(stats);
	C2_UT_ASSERT(is_in_eps_neighborhood(expected->se_avg, d));

	d = c2_net_test_stats_stddev(stats);
	C2_UT_ASSERT(is_in_eps_neighborhood(expected->se_stddev, d));
#endif
}

static void stats_serialize_ut(struct c2_net_test_stats *stats)
{
	struct c2_net_test_stats stats2;
	char			 bv_buf[STATS_BUF_LEN];
	void			*bv_addr = bv_buf;
	c2_bcount_t		 bv_len = STATS_BUF_LEN;
	struct c2_bufvec	 bv = C2_BUFVEC_INIT_BUF(&bv_addr, &bv_len);
	c2_bcount_t		 serialized_len;
	c2_bcount_t		 len;
	struct stats_expected	 expected;

	serialized_len = c2_net_test_stats_serialize(C2_NET_TEST_SERIALIZE,
						     stats, &bv,
						     STATS_BUF_OFFSET);
	C2_UT_ASSERT(serialized_len > 0);
	C2_SET0(&stats2);

	len = c2_net_test_stats_serialize(C2_NET_TEST_DESERIALIZE,
					  &stats2, &bv, STATS_BUF_OFFSET);
	C2_UT_ASSERT(len == serialized_len);

	expected.se_count  = stats->nts_count;
	expected.se_min	   = c2_net_test_stats_min(stats);
	expected.se_max	   = c2_net_test_stats_max(stats);
#ifndef __KERNEL__
	expected.se_sum	   = c2_net_test_stats_sum(stats);
	expected.se_avg	   = c2_net_test_stats_avg(stats);
	expected.se_stddev = c2_net_test_stats_stddev(stats);
#endif
	sample_check(&stats2, &expected);
}

static void add_one_by_one(struct c2_net_test_stats *stats,
			   unsigned long *arr,
			   unsigned long arr_len)
{
	unsigned long i;

	C2_PRE(stats != NULL);
	for (i = 0; i < arr_len; ++i) {
		c2_net_test_stats_add(stats, arr[i]);
		stats_serialize_ut(stats);
	}
}

#define STATS_SAMPLE(sample_name)					\
	static unsigned long sample_name ## _sample[]

#define STATS__EXPECTED(sample_name, count, min, max, sum, avg, stddev) \
	static struct stats_expected sample_name ## _expected = {	\
		.se_count  = (count),					\
		.se_min	   = (min),					\
		.se_max    = (max),					\
		.se_sum    = (sum),					\
		.se_avg    = (avg),					\
		.se_stddev = (stddev),					\
	}

#ifndef __KERNEL__
#define STATS_EXPECTED STATS__EXPECTED
#else
#define STATS_EXPECTED(sample_name, count, min, max, sum, avg, stddev) \
	STATS__EXPECTED(sample_name, count, min, max, 0, 0, 0)
#endif

#define STATS_SAMPLE_ADD(stats_name, sample_name)			\
	do {								\
		add_one_by_one(stats_name, sample_name ## _sample,	\
		ARRAY_SIZE(sample_name ## _sample));			\
	} while (0)

#define STATS_CHECK(stats_name, sample_name) \
	sample_check(stats_name, &sample_name ## _expected)

#define STATS_ADD_CHECK(stats_name, sample_name)			\
	do {								\
		STATS_SAMPLE_ADD(stats_name, sample_name);		\
		STATS_CHECK(stats_name, sample_name);			\
	} while (0)


STATS_SAMPLE(one_value) = { 1 };
STATS_EXPECTED(one_value, 1, 1, 1, 1., 1., 0.);

STATS_SAMPLE(five_values) = { 1, 2, 3, 4, 5 };
STATS_EXPECTED(five_values, 5, 1, 5, 15., 3., 1.58113883);

STATS_EXPECTED(zero_values, 0, 0, 0, 0., 0., 0.);

STATS_EXPECTED(million_values, STATS_ONE_MILLION,
	       ULONG_MAX, ULONG_MAX, 1. * ULONG_MAX * STATS_ONE_MILLION,
	       ULONG_MAX, 0.);

STATS_EXPECTED(one_plus_five_values, 6, 1, 5, 16., 16./6, 1.632993161);

static void stats_time_ut(void)
{
	struct c2_net_test_stats stats;
	c2_time_t		 time;
	int			 i;

	c2_net_test_stats_reset(&stats);
	/* sample: .5s, 1.5s, 2.5s, 3.5s, 4.5s */
	for (i = 0; i < 5; ++i) {
		c2_time_set(&time, i, 500000000);
		c2_net_test_stats_time_add(&stats, time);
		stats_serialize_ut(&stats);
	}
	/* check */
	time = c2_net_test_stats_time_min(&stats);
	C2_UT_ASSERT(c2_time_seconds(time) == 0);
	C2_UT_ASSERT(c2_time_nanoseconds(time) == 500000000);
	time = c2_net_test_stats_time_max(&stats);
	C2_UT_ASSERT(c2_time_seconds(time) == 4);
	C2_UT_ASSERT(c2_time_nanoseconds(time) == 500000000);
#ifndef __KERNEL__
	time = c2_net_test_stats_time_sum(&stats);
	C2_UT_ASSERT(c2_time_seconds(time) == 12);
	C2_UT_ASSERT(c2_time_nanoseconds(time) == 500000000);
	time = c2_net_test_stats_time_avg(&stats);
	C2_UT_ASSERT(c2_time_seconds(time) == 2);
	C2_UT_ASSERT(c2_time_nanoseconds(time) == 500000000);
	time = c2_net_test_stats_time_stddev(&stats);
	C2_UT_ASSERT(c2_time_seconds(time) == 1);
	C2_UT_ASSERT(581138830 <= c2_time_nanoseconds(time) &&
		     581138840 >= c2_time_nanoseconds(time));
#endif
}

void c2_net_test_stats_ut(void)
{
	struct c2_net_test_stats stats;
	struct c2_net_test_stats stats2;
	int			 i;

	/* test #0: no elements in sample */
	c2_net_test_stats_reset(&stats);
	STATS_CHECK(&stats, zero_values);
	stats_serialize_ut(&stats);
	/* test #1: one value in sample */
	c2_net_test_stats_reset(&stats);
	STATS_ADD_CHECK(&stats, one_value);
	/* test #2: five values in sample */
	c2_net_test_stats_reset(&stats);
	STATS_ADD_CHECK(&stats, five_values);
	/* test #3: one million identical values */
	c2_net_test_stats_reset(&stats);
	for (i = 0; i < STATS_ONE_MILLION; ++i)
		c2_net_test_stats_add(&stats, ULONG_MAX);
	STATS_CHECK(&stats, million_values);
	/* test #4: six values */
	c2_net_test_stats_reset(&stats);
	STATS_SAMPLE_ADD(&stats, one_value);
	STATS_SAMPLE_ADD(&stats, five_values);
	STATS_CHECK(&stats, one_plus_five_values);
	/* test #5: merge two stats */
	c2_net_test_stats_reset(&stats);
	STATS_SAMPLE_ADD(&stats, one_value);
	c2_net_test_stats_reset(&stats2);
	STATS_SAMPLE_ADD(&stats2, five_values);
	c2_net_test_stats_add_stats(&stats, &stats2);
	STATS_CHECK(&stats, one_plus_five_values);
	/* test #6: merge two stats, second is empty */
	c2_net_test_stats_reset(&stats);
	STATS_SAMPLE_ADD(&stats, one_value);
	c2_net_test_stats_reset(&stats2);
	c2_net_test_stats_add_stats(&stats, &stats2);
	STATS_CHECK(&stats, one_value);
	/* test #7: merge two stats, first is empty */
	c2_net_test_stats_reset(&stats);
	c2_net_test_stats_reset(&stats2);
	STATS_SAMPLE_ADD(&stats2, five_values);
	c2_net_test_stats_add_stats(&stats, &stats2);
	STATS_CHECK(&stats, five_values);
	/* test #8: test stats_time functions */
	stats_time_ut();
}

void c2_net_test_timestamp_ut(void)
{
	struct c2_net_test_timestamp ts;
	struct c2_net_test_timestamp ts1;
	c2_time_t		     before;
	c2_time_t		     after;
	c2_bcount_t		     serialized_len;
	c2_bcount_t		     len;
	char			     bv_buf[TIMESTAMP_BUF_LEN];
	void			    *bv_addr = bv_buf;
	c2_bcount_t		     bv_len = TIMESTAMP_BUF_LEN;
	struct c2_bufvec	     bv = C2_BUFVEC_INIT_BUF(&bv_addr, &bv_len);

	before = c2_time_now();
	c2_net_test_timestamp_init(&ts, TIMESTAMP_SEQ);
	after = c2_time_now();

	C2_UT_ASSERT(c2_time_after_eq(ts.ntt_time, before));
	C2_UT_ASSERT(c2_time_after_eq(after, ts.ntt_time));
	C2_UT_ASSERT(ts.ntt_magic == C2_NET_TEST_TIMESTAMP_MAGIC);
	C2_UT_ASSERT(ts.ntt_seq == TIMESTAMP_SEQ);

	serialized_len = c2_net_test_timestamp_serialize(C2_NET_TEST_SERIALIZE,
							 &ts, &bv, 0);
	C2_UT_ASSERT(serialized_len > 0);
	C2_SET0(&ts1);
	len = c2_net_test_timestamp_serialize(C2_NET_TEST_DESERIALIZE,
					      &ts1, &bv, 0);
	C2_UT_ASSERT(serialized_len = len);
	C2_UT_ASSERT(c2_time_after_eq(ts.ntt_time, ts1.ntt_time) &&
		     c2_time_after_eq(ts1.ntt_time, ts.ntt_time));
	C2_UT_ASSERT(ts1.ntt_magic == ts.ntt_magic);
	C2_UT_ASSERT(ts1.ntt_seq == ts.ntt_seq);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
