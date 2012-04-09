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
   @page net-test-stats-fspec Functional Specification

   - @ref net-test-stats-fspec-ds
   - @ref net-test-stats-fspec-sub
   - @ref net-test-stats-fspec-cli
   - @ref net-test-stats-fspec-usecases
   - @subpage NetTestStatsDFS "Detailed Functional Specification"
   - @subpage NetTestStatsInternals "Internals"

   @section net-test-stats-fspec-ds Data Structures

   - c2_net_test_stats

   @section net-test-stats-fspec-sub Subroutines
   <i>Mandatory for programmatic interfaces.  Components with programming
   interfaces should provide an enumeration and brief description of the
   externally visible programming interfaces.</i>

   @subsection net-test-stats-fspec-sub-cons Constructors and Destructors

   - c2_net_test_stats_init()
   - c2_net_test_stats_init_zero()
   - c2_net_test_stats_fini()

   @subsection net-test-stats-fspec-sub-acc Accessors and Invariants

   @subsection net-test-stats-fspec-sub-opi Operational Interfaces

   - c2_net_test_stats_add()
   - c2_net_test_stats_add_stats()

   - c2_net_test_stats_min()
   - c2_net_test_stats_max()
   - c2_net_test_stats_avg()
   - c2_net_test_stats_stddev()
   - c2_net_test_stats_count()

   @section net-test-stats-fspec-cli Command Usage
   <i>Mandatory for command line programs.  Components that provide programs
   would provide a specification of the command line invocation arguments.  In
   addition, the format of any any structured file consumed or produced by the
   interface must be described in this section.</i>

   @section net-test-stats-fspec-usecases Recipes
   <i>This section could briefly explain what sequence of interface calls or
   what program invocation flags are required to solve specific usage
   scenarios.  It would be very nice if these examples can be linked
   back to the HLD for the component.</i>

   @see
   @ref net-test-stats @n
   @ref NetTestStatsDFS "Detailed Functional Specification" @n
   @ref NetTestStatsInternals "Internals" @n
 */

/**
   @defgroup NetTestStatsDFS Colibri Network Benchmark Statistics Collector

   @see
   @ref net-test-stats

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
	/** min value from sample */
	double min;
	/** max value from sample */
	double max;
	/** sum of all values from sample */
	double sum;
	/** sum of squares of all values from sample */
	double sum_sqr;
	/** sample size */
	long   count;
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
void c2_net_test_stats_init(struct c2_net_test_stats *stats, double min,
		double max, double sum, double sum_sqr, long count);

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
void c2_net_test_stats_add(struct c2_net_test_stats *stats, const double value);

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
   Get a smalles value from a given sample.
 */
double c2_net_test_stats_min(const struct c2_net_test_stats *stats);

/**
   Get a largest value from a given sample.
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
   @} end NetTestStatsDFS
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
