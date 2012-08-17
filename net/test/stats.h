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

#include "lib/time.h"		/* c2_time_t */
#include "net/test/uint256.h"	/* c2_net_test_uint256 */

#ifndef __KERNEL__
#include "net/test/user_space/stats_u.h"	/* c2_net_test_stats_avg */
#endif

/**
   @defgroup NetTestStatsDFS Statistics Collector
   @ingroup NetTestDFS

   Arithmetic mean calculation (LaTeX):
   \f[ \overline{x} = \frac {1}{N} \sum\limits_{i=1}^N {x_i} \f]
   It is assumed that arithmetic mean = 0 if N == 0.

   Sample standard deviation calculation:
   \f[ s =
       \sqrt {\frac {1}{N - 1}  \sum\limits_{i=1}^N {(x_i - \overline{x})^2} } =
       \sqrt {\frac {1}{N - 1} (\sum\limits_{i=1}^N {x_i^2} +
			        \sum\limits_{i=1}^N {\overline{x}^2} -
			      2 \sum\limits_{i=1}^N {x_i \overline{x}} )} =
       \sqrt {\frac {1}{N - 1} (\sum\limits_{i=1}^N {x_i^2} +
			      N {\overline{x}^2} -
			      2 \overline{x} \sum\limits_{i=1}^N {x_i})}
   \f]
   We have \f$ N \overline{x} = \sum\limits_{i=1}^N {x_i} \f$, so
   \f[ s =
       \sqrt {\frac {1}{N - 1} (\sum\limits_{i=1}^N {x_i^2} +
			      N {\overline{x}^2} -
			      2 \overline{x} \cdot N \overline{x} )} =
       \sqrt {\frac {1}{N - 1} (\sum\limits_{i=1}^N {x_i^2} -
			      N {\overline{x}^2})}
   \f]
   It is assumed that sample standard deviation = 0 if N == 0 || N == 1.

   @see
   @ref net-test

   @{
 */

enum {
	/** NT_TIMES */
	C2_NET_TEST_TIMESTAMP_MAGIC = 0x53454d49545f544e,
};

/**
   This structure is used for statistics calculation and collecting.
   Min and max stored directly in this structure, average and
   standard deviation can be calculated using this structure.
   When the new value is added to sample, c2_net_test_stats is
   updating according to this.

   int64_t isn't enough to hold sum value (if it is number of bytes).
   Reason:
   100'000 (hosts) * 37'500'000 (bytes per second) EDR 12x Infiniband *
   1000 (2012-2032 moore's law network speed increase) =
   3.75 * 10^15 bytes per second in the cluster
   ~3.1 * 10^7 seconds in year => ~10^23 bytes can be in sum field.
 */
struct c2_net_test_stats {
	/** sample size */
	unsigned long		   nts_count;
	/** min value from sample */
	unsigned long		   nts_min;
	/** max value from sample */
	unsigned long		   nts_max;
	/** sum of all values from sample */
	struct c2_net_test_uint256 nts_sum;
	/** sum of squares of all values from sample */
	struct c2_net_test_uint256 nts_sum_sqr;
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
   @post c2_net_test_stats_invariant(stats)
 */
void c2_net_test_stats_init(struct c2_net_test_stats *stats);

/**
   Invariant for c2_net_test_stats.
 */
bool c2_net_test_stats_invariant(const struct c2_net_test_stats *stats);

/**
   Add value to sample.
   @pre c2_net_test_stats_invariant(stats)
 */
void c2_net_test_stats_add(struct c2_net_test_stats *stats,
			   unsigned long value);

/**
   Merge two samples and write result to *stats.
   Subsequents calls to c2_net_test_stats_FUNC will be the same as
   if all values, which was added to the second sample (stats2),
   would be added to the first sample (stats). Result is stored
   in the first sample structure.
   @pre c2_net_test_stats_invariant(stats)
   @pre c2_net_test_stats_invariant(stats2)
 */
void c2_net_test_stats_add_stats(struct c2_net_test_stats *stats,
				 const struct c2_net_test_stats *stats2);

/**
   Get sample size, in elements.
   @pre c2_net_test_stats_invariant(stats)
 */
unsigned long c2_net_test_stats_count(const struct c2_net_test_stats *stats);

/**
   Get the smallest value from a given sample.
   @pre c2_net_test_stats_invariant(stats)
 */
unsigned long c2_net_test_stats_min(const struct c2_net_test_stats *stats);

/**
   Get the largest value from a given sample.
   @pre c2_net_test_stats_invariant(stats)
 */
unsigned long c2_net_test_stats_max(const struct c2_net_test_stats *stats);

/**
   Serialize/deserialize c2_net_test_stats.
   @see c2_net_test_serialize().
 */
c2_bcount_t c2_net_test_stats_serialize(enum c2_net_test_serialize_op op,
					struct c2_net_test_stats *stats,
					struct c2_bufvec *bv,
					c2_bcount_t bv_offset);

/**
   The same as c2_net_test_stats_add(), but using c2_time_t as
   sample element. Next function can be used after this function to
   obtain c2_time_t results:
   - c2_net_test_stats_time_min()
   - c2_net_test_stats_time_max()
   - c2_net_test_stats_time_avg()
   - c2_net_test_stats_time_stddev()
   Mixing c2_net_test_stats_add() and c2_net_test_stats_time_add()
   will lead to undefined behavior.
 */
void c2_net_test_stats_time_add(struct c2_net_test_stats *stats,
				c2_time_t time);

/**
   @see c2_net_test_stats_time_add()
 */
c2_time_t c2_net_test_stats_time_min(struct c2_net_test_stats *stats);

/**
   @see c2_net_test_stats_time_add()
 */
c2_time_t c2_net_test_stats_time_max(struct c2_net_test_stats *stats);

/**
   Timestamp.
   Used to transmit c2_time_t value in ping/bulk buffers.
   @see c2_net_test_timestamp_init(), c2_net_test_timestamp_serialize(),
   c2_net_test_timestamp_get().
   @todo create net/test/timestamp.[ch] and net/test/ut/timestamp.c
 */
struct c2_net_test_timestamp {
	/** Current time. Set in c2_net_test_timestamp_init() */
	c2_time_t ntt_time;
	/** Magic. Checked when deserializing. */
	uint64_t  ntt_magic;
};

/**
   Initialize timestamp.
   Set timestamp time to c2_time_now().
   @pre t != NULL
 */
void c2_net_test_timestamp_init(struct c2_net_test_timestamp *t);

/**
   Serialize/deserialize timestamp.
   Deserialize will fail if magic mismatch.
   @see c2_net_test_serialize().
 */
c2_bcount_t c2_net_test_timestamp_serialize(enum c2_net_test_serialize_op op,
					    struct c2_net_test_timestamp *t,
					    struct c2_bufvec *bv,
					    c2_bcount_t bv_offset);
/**
   Get time from timestamp.
   @pre t != NULL
 */
c2_time_t c2_net_test_timestamp_get(struct c2_net_test_timestamp *t);

/**
   @} end of NetTestStatsDFS group
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
