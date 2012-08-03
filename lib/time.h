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

#ifndef __COLIBRI_LIB_TIME_H__
#define __COLIBRI_LIB_TIME_H__

#include "lib/types.h"

/**
   @defgroup time Generic time manipulation

   C2 time delivers resolution in nanoseconds. It is an unsigned 64-bit integer.
   @{
 */

#ifndef __KERNEL__
#include "lib/user_space/time.h"
#else
#include "lib/linux_kernel/time.h"
#endif

typedef uint64_t c2_time_t;

enum {
	C2_TIME_ONE_BILLION = 1000000000ULL
};

/**
   Get the current time.  This may or may not relate to wall time.

   @retval current time
 */
c2_time_t c2_time_now(void);

/**
   Create a c2_time_t initialized with seconds + nanosecond in the future.

   @param secs seconds from now
   @param ns nanoseconds from now

   @retval the result time.
 */
c2_time_t c2_time_from_now(uint64_t secs, long ns);

c2_time_t c2_time(uint64_t secs, long ns);

/**
   Create a c2_time_t from seconds and nanosecond

   @param time [OUT] the result time.
   @param secs seconds from epoch
   @param ns nanoseconds
   @retval the result time.
 */
c2_time_t c2_time_set(c2_time_t *time, uint64_t secs, long ns);

/**
   Add t2 to t1, and return that result

   @retval the result time
 */
c2_time_t c2_time_add(const c2_time_t t1, const c2_time_t t2);

/**
   Subtract t2 from t1, and return that result

   @retval the result time
 */
c2_time_t c2_time_sub(const c2_time_t t1, const c2_time_t t2);

/**
   Sleep for requested time. If interrupted, remaining time returned.

   @param req requested time to sleep
   @param rem [OUT] remaining time, NULL causes remaining time to be ignored.
   @retval 0 means success. -1 means error. remaining time will be stored
           in rem.
 */
int c2_nanosleep(const c2_time_t req, c2_time_t *rem);

/**
   Get "second" part from the time

   @retval second part of the time
 */
uint64_t c2_time_seconds(const c2_time_t time);

/**
   Get "nanosecond" part from the time

   @retval nanosecond part of the time
 */
uint64_t c2_time_nanoseconds(const c2_time_t time);

/**
   the biggest time that never reaches in system life.
 */
extern const c2_time_t C2_TIME_NEVER;

/** @} end of time group */

/* __COLIBRI_LIB_TIME_H__ */
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
