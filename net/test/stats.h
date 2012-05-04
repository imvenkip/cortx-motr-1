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
 * Original author Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 03/22/2012
 */

#ifndef __NET_TEST_STATS_H__
#define __NET_TEST_STATS_H__

/**
   @defgroup NetTestStatsDFS Colibri Network Benchmark \
			     Statistics Collector \
			     Detailed Functional Specification

   @see
   @ref net-test

   @{
 */

/**
   This structure is used for statistics calculation and collecting.
   Min and max stored directly in this structure, average and
   standard deviation can be calculated using this structure.
   When the new value is added to sample, c2_net_test_stats is
   updating according to this.
 */
struct c2_net_test_stats {
	unsigned long min;	/** min value from sample */
	unsigned long max;	/** max value from sample */
	unsigned long sum;	/** sum of all values from sample */
	unsigned long sum_sqr;	/** sum of squares of all values from sample */
	long	      count;	/** sample size */
};

/**
   Static initializer for c2_net_test_stats
   @hideinitializer
 */
#define C2_NET_TEST_STATS_DEFINE(min, max, sum, sum_sqr, count) { \
	.min = (min), \
	.max = (max), \
	.sum = (sum), \
	.sum_sqr = (sum_sqr), \
	.count = (count) \
}

/**
   Initializer for c2_net_test_stats structure.
 */
void c2_net_test_stats_init(struct c2_net_test_stats *stats,
		const unsigned long min,
		const unsigned long max,
		const unsigned long sum,
		const unsigned long sum_sqr,
		const long count);

/**
   Initialize c2_net_test_stats structure to zero size sample.
 */
void c2_net_test_stats_init_zero(struct c2_net_test_stats *stats);

/**
   Finalizer for c2_net_test_stats structure.
 */
void c2_net_test_stats_fini(struct c2_net_test_stats *stats);

/**
   Add value to sample.
 */
void c2_net_test_stats_add(struct c2_net_test_stats *stats,
		const unsigned long value);

/**
   Merge two samples and write result to *stats.
   Subsequents calls to c2_net_test_stats_FUNC will be the same as
   if all values, which was added to the second sample (stats2),
   would be added to the first sample (stats). Result is stored
   in the first sample structure.
 */
void c2_net_test_stats_add_stats(struct c2_net_test_stats *stats,
			   const struct c2_net_test_stats *stats2);

/**
   Get the smallest value from a given sample.
 */
double c2_net_test_stats_min(const struct c2_net_test_stats *stats);

/**
   Get the largest value from a given sample.
 */
double c2_net_test_stats_max(const struct c2_net_test_stats *stats);

/**
   Get sample average (arithmetic mean).
 */
double c2_net_test_stats_avg(const struct c2_net_test_stats *stats);

/**
   Get sample standard deviation.
 */
double c2_net_test_stats_stddev(const struct c2_net_test_stats *stats);

/**
   Get sample size, in elements.
 */
long   c2_net_test_stats_count(const struct c2_net_test_stats *stats);

/**
   @} end of NetTestStatsDFS
 */

#endif /*  __NET_TEST_STATS_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
