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
 * Original author: Nathan Rutman <nathan_rutman@xyratex.com>
 *		    Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 12/06/2010
 */

#include "lib/time.h"
#include "lib/assert.h"  /* M0_CASSERT */
#include "lib/cdefs.h"   /* M0_EXPORTED */
#include <linux/module.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/sched.h>

/**
   @addtogroup time

   <b>Implementation of m0_time_t on top of kernel struct timespec

   @{
*/

m0_time_t m0_time_now(void)
{
	struct timespec ts;
	m0_time_t	t;

	ts = current_kernel_time();
	m0_time_set(&t, ts.tv_sec,  ts.tv_nsec);

	return t;
}
M0_EXPORTED(m0_time_now);

/**
   Sleep for requested time
*/
M0_INTERNAL int m0_nanosleep(const m0_time_t req, m0_time_t * rem)
{
	struct timespec ts = {
			.tv_sec  = m0_time_seconds(req),
			.tv_nsec = m0_time_nanoseconds(req)
		};
	int rc = 0;
	unsigned long tj = timespec_to_jiffies(&ts);

	/* this may use TASK_INTERRUPTIBLE to capture signals */
        set_current_state(TASK_UNINTERRUPTIBLE);
        schedule_timeout(tj);
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
