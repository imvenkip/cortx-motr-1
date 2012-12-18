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
 * Original author: Nathan Rutman <Nathan_Rutman@xyratex.com>,
 *                  Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 12/10/2010
 */

#pragma once

#ifndef __MERO_LIB_TIME_H__
#define __MERO_LIB_TIME_H__

#include "lib/types.h"

/**
   @defgroup time Generic time manipulation

   M0 time delivers resolution in nanoseconds. It is an unsigned 64-bit integer.
   @{
 */

#ifndef __KERNEL__
#include "lib/user_space/time.h"
#else
#include "lib/linux_kernel/time.h"
#endif

typedef uint64_t m0_time_t;

enum {
	M0_TIME_ONE_BILLION = 1000000000ULL
};

#define M0_MKTIME(secs, ns) ((m0_time_t)				\
			    ((uint64_t)(secs) * M0_TIME_ONE_BILLION +	\
			    (uint64_t)(ns)))
#define M0_MKTIME_HOURS(hours, mins, secs, ns)				\
		((m0_time_t)						\
		 ((uint64_t)((hours) * 60 * 60 +			\
			     (mins) * 60 +				\
			     (secs)) * M0_TIME_ONE_BILLION +		\
		  (uint64_t)(ns)))

/**
   Get the current time.  This may or may not relate to wall time.

   @return The current time.
 */
m0_time_t m0_time_now(void);

/**
   Create a m0_time_t initialized with seconds + nanosecond in the future.

   @param secs seconds from now
   @param ns nanoseconds from now

   @return The result time.
 */
m0_time_t m0_time_from_now(uint64_t secs, long ns);

/**
   Create and return a m0_time_t from seconds and nanoseconds.
 */
m0_time_t m0_time(uint64_t secs, long ns);

/**
   Create a m0_time_t from seconds and nanoseconds.

   @param time [OUT] the result time.
   @param secs Seconds from epoch.
   @param ns Nanoseconds.
   @retval the result time.
 */
m0_time_t m0_time_set(m0_time_t * time, uint64_t secs, long ns);

/**
   Add t2 to t1 and return that result.

   @return The result time. If either t1 or t2 is M0_TIME_NEVER, the result
   is M0_TIME_NEVER.
 */
m0_time_t m0_time_add(const m0_time_t t1, const m0_time_t t2);

/**
   Subtract t2 from t1 and return that result.

   @return The result time. If t1 == M0_TIME_NEVER, M0_TIME_NEVER is returned.
   @pre t2 < M0_TIME_NEVER && t1 >= t2
 */
m0_time_t m0_time_sub(const m0_time_t t1, const m0_time_t t2);

/**
   Sleep for requested time. If interrupted, remaining time returned.

   @param req requested time to sleep
   @param rem [OUT] remaining time, NULL causes remaining time to be ignored.
   @return 0 means success. -1 means error. Remaining time is stored in rem.
 */
int m0_nanosleep(const m0_time_t req, m0_time_t * rem);

/**
   Get "second" part from the time.
 */
uint64_t m0_time_seconds(const m0_time_t time);

/**
   Get "nanosecond" part from the time.
 */
uint64_t m0_time_nanoseconds(const m0_time_t time);

bool m0_time_is_in_past(m0_time_t time);

/**
   The largest time that is never reached in system life.
 */
extern const m0_time_t M0_TIME_NEVER;

/** @} end of time group */

/* __MERO_LIB_TIME_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
