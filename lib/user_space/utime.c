/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nathan Rutman <Nathan_Rutman@us.xyratex.com>,
 *                  Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 01/27/2011
 */

#include "lib/time.h"
#include "lib/assert.h"  /* C2_CASSERT */
#include "lib/cdefs.h"   /* C2_EXPORTED */
#include <stddef.h>

extern int nanosleep(const struct timespec *req, struct timespec *rem);

/**
   @addtogroup time

   Implementation of c2_time_t on top of userspace struct timespec

   @{
*/

c2_time_t c2_time_now(c2_time_t *time)
{
        struct timeval tv;

        C2_PRE(time != NULL);
        /* We could use clock_gettime(CLOCK_REALTIME, time) for nanoseconds,
         but we would have to link librt... */
        gettimeofday(&tv, NULL);
        c2_time_set(time, tv.tv_sec, tv.tv_usec * 1000);
	return *time;
}
C2_EXPORTED(c2_time_now);

/**
   Sleep for requested time
*/
int c2_nanosleep(const c2_time_t req, c2_time_t *rem)
{
	struct timespec reqts = {
			.tv_sec  = c2_time_seconds(req),
			.tv_nsec = c2_time_nanoseconds(req)
			};
	struct timespec remts = { 0 };
	int rc;

	rc = nanosleep(&reqts, &remts);
	if (rem != NULL)
		c2_time_set(rem, remts.tv_sec, remts.tv_nsec);
	return rc;
}
C2_EXPORTED(c2_nanosleep);


/** @} end of time group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
