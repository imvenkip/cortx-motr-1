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

#pragma once

#ifndef __COLIBRI_NET_TEST_STATS_H__
#define __COLIBRI_NET_TEST_STATS_H__

#include "lib/time.h"		/* c2_time_t */
#include "lib/atomic.h"		/* c2_atomic64 */
#include "lib/misc.h"		/* C2_SET0 */

#include "net/test/serialize.h"	/* c2_net_test_serialize */

#ifndef __KERNEL__
#include "net/test/user_space/stats_u.h"	/* c2_net_test_stats_avg */
#endif

/**
   @defgroup NetTestStatsDFS Statistics Collector
   @ingroup NetTestDFS

   @todo Move to lib/stats.h

   Arithmetic mean calculation:
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

   Constraints:
   - Sample size \f$ \in [0, 2^{64} - 1] \f$;
   - Any value from sample \f$ \in [0, 2^{64} - 1] \f$;
   - Min and max value from sample \f$ \in [0, 2^{64} - 1] \f$;
   - Sum of all values from sample \f$ \in [0, 2^{128} - 1] \f$;
   - Sum of squares of all values from sample \f$ \in [0, 2^{128} - 1] \f$.

   @see
   @ref net-test

   @{
 */

enum {
	/** NT_TIMES @todo move to lib/magic.h */
	C2_NET_TEST_TIMESTAMP_MAGIC = 0x53454d49545f544e,
};

/**
   This structure is used for statistics calculation and collecting.
   Min and max stored directly in this structure, average and
   standard deviation can be calculated using this structure.
   When the new value is added to sample, c2_net_test_stats is
   updating according to this.
 */
struct c2_net_test_stats {
	/** sample size */
	unsigned long	  nts_count;
	/** min value from sample */
	unsigned long	  nts_min;
	/** max value from sample */
	unsigned long	  nts_max;
	/** sum of all values from sample */
	struct c2_uint128 nts_sum;
	/** sum of squares of all values from sample */
	struct c2_uint128 nts_sum_sqr;
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
   Reset c2_net_test_stats structure.
   @post c2_net_test_stats_invariant(stats)
 */
void c2_net_test_stats_reset(struct c2_net_test_stats *stats);

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
   - c2_net_test_stats_time_sum()
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
   @} end of NetTestStatsDFS group
 */

/**
   @defgroup NetTestTimestampDFS Timestamp
   @ingroup NetTestDFS

   Used to transmit c2_time_t value in ping/bulk buffers.
   @see c2_net_test_timestamp_init(), c2_net_test_timestamp_serialize().
 */
struct c2_net_test_timestamp {
	/** Current time. Set in c2_net_test_timestamp_init() */
	c2_time_t ntt_time;
	/** Sequence number. */
	uint64_t  ntt_seq;
	/** Magic. Checked when deserializing. */
	uint64_t  ntt_magic;
};

/**
   Initialize timestamp.
   Set c2_net_test_timestamp.ntt_time to c2_time_now().
   @param t timestamp structure
   @param seq sequence number
   @pre t != NULL
 */
void c2_net_test_timestamp_init(struct c2_net_test_timestamp *t, uint64_t seq);

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
   @} end of NetTestTimestampDFS group
 */

/**
   @defgroup NetTestStatsMPSDFS Messages Per Second Statistics
   @ingroup NetTestDFS

   @see
   @ref net-test

   @{
 */

/** Messages Per Second statistics */
struct c2_net_test_mps {
	/** Statistics */
	struct c2_net_test_stats ntmps_stats;
	/** Last check number of messages */
	unsigned long		 ntmps_last_nr;
	/** Last check time */
	c2_time_t		 ntmps_last_time;
	/** Time interval to check */
	c2_time_t		 ntmps_time_interval;
};

/**
   Initialize MPS statistics.
   @param mps MPS statistics structure.
   @param messages Next call to c2_net_test_mps_add() will use
		   this value as previous value to measure number of messages
		   transferred in time interval.
   @param timestamp The same as messages, but for time difference.
   @param interval MPS measure interval.
		   c2_net_test_mps_add() will not add sample
		   to stats if time from last addition to statistics
		   is less than interval.
 */
void c2_net_test_mps_init(struct c2_net_test_mps *mps,
			  unsigned long messages,
			  c2_time_t timestamp,
			  c2_time_t interval);

/**
   Add sample to the MPS statistics if time interval
   [mps->ntmps_last_time, timestamp] is greater than mps->ntmps_interval.
   This function will use previous call (or initializer) parameters to
   calculate MPS: number of messages [mps->ntmps_last_nr, messages]
   in the time range [mps->ntmps_last_time, timestamp].
   @param mps MPS statistics structure.
   @param messages Total number of messages transferred.
   @param timestamp Timestamp of messages value.
   @return Value will not be added to the sample before this time.
 */
c2_time_t c2_net_test_mps_add(struct c2_net_test_mps *mps,
			      unsigned long messages,
			      c2_time_t timestamp);

/**
   @} end of NetTestStatsMPSDFS group
 */

/**
   @defgroup NetTestMsgNRDFS Messages Number
   @ingroup NetTestDFS

   @{
 */

/** Sent/received test messages number. */
struct c2_net_test_msg_nr {
	/**
	 * Total number of test messages.
	 * Increased after callback executed for the message buffer.
	 */
	size_t ntmn_total;
	/**
	 * Number of failed (network failures) test messages.
	 * Increased if c2_net_buffer_add() failed or
	 * (c2_net_buffer_event.nbe_status != 0 &&
	 *  c2_net_buffer_event.nbe_status != -ECANCELED)
	 * in buffer completion callback.
	 */
	size_t ntmn_failed;
	/**
	 * Number of bad test messages (invalid message content)
	 * Increased if message deserializing failed or message
	 * contains invalid data.
	 */
	size_t ntmn_bad;
};

/**
   Reset all messages number statistics to 0.
 */
static inline void c2_net_test_msg_nr_reset(struct c2_net_test_msg_nr *msg_nr)
{
	C2_SET0(msg_nr);
}

/**
   Accumulate messages number.
 */
void c2_net_test_msg_nr_add(struct c2_net_test_msg_nr *msg_nr,
			    const struct c2_net_test_msg_nr *msg_nr2);

/**
   @} end of NetTestMsgNRDFS group
 */

#endif /*  __COLIBRI_NET_TEST_STATS_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
