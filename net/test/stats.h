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

#ifndef __NET_TEST_STATS_H__
#define __NET_TEST_STATS_H__

#include "lib/time.h"		/* c2_time_t */
#include "lib/atomic.h"		/* c2_atomic64 */

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

   Limits:
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
	/** NT_TIMES */
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

/**
   @defgroup NetTestStatsBandwidthDFS Bandwidth Statistics
   @ingroup NetTestDFS

   @see
   @ref net-test

   @{
 */

/** Bandwidth statistics, measured in bytes/sec */
struct c2_net_test_stats_bandwidth {
	/** Statistics */
	struct c2_net_test_stats ntsb_stats;
	/** Last check number of bytes */
	c2_bcount_t		 ntsb_bytes_last;
	/** Last check time */
	c2_time_t		 ntsb_time_last;
	/** Time interval to check */
	c2_time_t		 ntsb_time_interval;
};

/**
   Initialize bandwidth statistics.
   @param sb Bandwidth statistics structure.
   @param bytes Next call to c2_net_test_stats_bandwidth_add() will use
		this value as previous value to measure number of bytes
		transferred in time interval.
   @param timestamp The same as bytes, but for time difference.
   @param interval Bandwidth measure interval.
		   c2_net_test_stats_bandwidth_add() will not add sample
		   to stats if interval from last addition to statistics
		   is less than interval.
 */
void c2_net_test_stats_bandwidth_init(struct c2_net_test_stats_bandwidth *sb,
				      c2_bcount_t bytes,
				      c2_time_t timestamp,
				      c2_time_t interval);

/**
   Add sample to the bandwidth statistics if time interval
   [sb->ntsb_time_last, timestamp] is greater than sb->ntsb_interval.
   This function will use previous call (or initializer) parameters to
   calculate bandwidth: number of bytes [sb->ntsb_bytes_last, bytes]
   in the time range [sb->ntsb_time_last, timestamp].
   @param sb Bandwidth statistics structure.
   @param bytes Total number of bytes transferred.
   @param timestamp Timestamp of bytes value.
   @return Value will not be added to the sample before this time.
 */
c2_time_t
c2_net_test_stats_bandwidth_add(struct c2_net_test_stats_bandwidth *sb,
				c2_bcount_t bytes,
				c2_time_t timestamp);

/**
   @} end of NetTestStatsBandwidthDFS group
 */

/**
   @defgroup NetTestMsgNRDFS Messages Number
   @ingroup NetTestDFS

   @{
 */

struct c2_net_test_cmd_status_data;

/** Sent/received test messages number. */
struct c2_net_test_msg_nr {
	/** Number of sent test messages */
	struct c2_atomic64 ntmn_sent;
	/** Number of received test messages */
	struct c2_atomic64 ntmn_rcvd;
	/** Number of errors while receiving test messages */
	struct c2_atomic64 ntmn_send_failed;
	/** Number of errors while sending test messages */
	struct c2_atomic64 ntmn_recv_failed;
};

/**
   Reset all messages number statistics to 0.
 */
void c2_net_test_msg_nr_reset(struct c2_net_test_msg_nr *msg_nr);

/**
   Copy messages number statistics to c2_net_test_cmd_status_data.
   Algorithm:
   1. Copy statistics from sd to local variables, one by one field.
   2. Copy statistics from msg_nr to sd, one by one field.
   3. Compare values in sd and local variables - goto 1 if they aren't equal.
 */
void c2_net_test_msg_nr_get_lockfree(struct c2_net_test_msg_nr *msg_nr,
				     struct c2_net_test_cmd_status_data *sd);

/**
   @} end of NetTestMsgNRDFS group
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
