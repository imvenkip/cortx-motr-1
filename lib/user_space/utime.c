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
 * Original creation date: 01/27/2011
 */

#include "lib/time.h"
#include "lib/assert.h"  /* M0_CASSERT */
#include "lib/cdefs.h"   /* M0_EXPORTED */
#include <stddef.h>

int nanosleep(const struct timespec *req, struct timespec *rem);

/**
   @addtogroup time

   Implementation of m0_time_t on top of userspace struct timespec

   @{
*/

m0_time_t m0_time_now(void)
{
        struct timeval tv;
	m0_time_t      t;

        /* We could use clock_gettime(CLOCK_REALTIME, time) for nanoseconds,
         but we would have to link librt... */
        gettimeofday(&tv, NULL);
        m0_time_set(&t, tv.tv_sec, tv.tv_usec * 1000);

        return t;
}
M0_EXPORTED(m0_time_now);

/**
   Sleep for requested time
*/
int m0_nanosleep(const m0_time_t req, m0_time_t * rem)
{
	struct timespec reqts = {
			.tv_sec  = m0_time_seconds(req),
			.tv_nsec = m0_time_nanoseconds(req)
			};
	struct timespec remts = { 0 };
	int rc;

	rc = nanosleep(&reqts, &remts);
	if (rem != NULL)
		m0_time_set(rem, remts.tv_sec, remts.tv_nsec);
	return rc;
}
M0_EXPORTED(m0_nanosleep);


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
