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

/**
   @page net-test-stats Statistics Collector

   - @ref net-test-stats-ovw
   - @ref net-test-stats-def
   - @subpage net-test-stats-fspec "Functional Specification"
   - @ref net-test-stats-lspec
   - @ref net-test-stats-lspec-thread
   - @ref net-test-stats-lspec-numa
   - @ref net-test-stats-ut
   - @ref net-test-stats-O
   - @ref net-test-stats-ref

   <hr>
   @section net-test-stats-ovw Overview

   This document is intended to describe statistics collecting for
   @ref net-test.

   <hr>
   @section net-test-stats-def Definitions

   - <b>Sample</b> A set of values.
   - <b>Min</b> Smallest value from sample.
   - <b>Max</b> Largest value from sample.
   - <b>Average</b> Sample average (arithmetic mean).
   - <b>Standard deviation</b> Sample standard deviation.

   <hr>
   @section net-test-stats-lspec Logical Specification

   - c2_net_test_stats is used for keeping some data for sample,
   based on which min/max/average/standard deviation can be calculated.

   @subsection net-test-stats-lspec-thread Threading and Concurrency Model
   <i>Mandatory.
   This section describes the threading and concurrency model.
   It describes the various asynchronous threads of operation, identifies
   the critical sections and synchronization primitives used
   (such as semaphores, locks, mutexes and condition variables).</i>

   c2_net_test_stats are not protected by any synchronization mechanism.
   If you want to use it from more than one thread, make sure that access
   will be protected.

   @subsection net-test-stats-lspec-numa NUMA optimizations
   <i>Mandatory for components with programmatic interfaces.
   This section describes if optimal behavior can be supported by
   associating the utilizing thread to a single processor.</i>

   You can use one c2_net_test_stats per locality and merge statistics from
   all localities with c2_net_test_stats_add_stats() only when it needed.

   <hr>
   @section net-test-stats-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>
   @todo add

   <hr>
   @section net-test-stats-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   All c2_net_test_stats_* functions have O(1) complexity.

   <hr>
   @section net-test-stats-ref References
   <i>Mandatory. Provide references to other documents and components that
   are cited or used in the design.
   In particular a link to the HLD for the DLD should be provided.</i>

   @ref net-test

 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "net/test/stats.h"

/**
   @defgroup NetTestStatsInternals Colibri Network Bencmark Statistics Collector Internals

   @see
   @ref net-test-stats

   @{
 */

void c2_net_test_stats_init(struct c2_net_test_stats *stats, double min,
		double max, double sum, double sum_sqr, long count)
{
}

void c2_net_test_stats_init_zero(struct c2_net_test_stats *stats)
{
}

void c2_net_test_stats_fini(struct c2_net_test_stats *stats)
{
}

void c2_net_test_stats_add(struct c2_net_test_stats *stats, const double value)
{
}

void c2_net_test_stats_add_stats(struct c2_net_test_stats *stats,
			   const struct c2_net_test_stats *stats2)
{
}

double c2_net_test_stats_min(const struct c2_net_test_stats *stats)
{
}

double c2_net_test_stats_max(const struct c2_net_test_stats *stats)
{
}

double c2_net_test_stats_avg(const struct c2_net_test_stats *stats)
{
}

double c2_net_test_stats_stddev(const struct c2_net_test_stats *stats)
{
}

long   c2_net_test_stats_count(const struct c2_net_test_stats *stats)
{
}

/**
   @} end NetTestStatsInternals
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
