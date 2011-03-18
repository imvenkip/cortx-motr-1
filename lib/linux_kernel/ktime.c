/* -*- C -*- */

#include "lib/time.h"
#include "lib/assert.h"  /* C2_CASSERT */
#include "lib/cdefs.h"   /* C2_EXPORTED */
#include <linux/module.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/sched.h>

/**
   @addtogroup time

   <b>Implementation of c2_time on top of kernel struct timespec

   @{
*/

struct c2_time *c2_time_now(struct c2_time *time)
{
	C2_PRE(time != NULL);
        time->ts = current_kernel_time();
	return time;
}
C2_EXPORTED(c2_time_now);

/**
   Sleep for requested time
*/
int c2_nanosleep(const struct c2_time *req, struct c2_time *rem)
{
	int rc = 0;
	unsigned long tj = timespec_to_jiffies(&req->ts);

	/* this may use TASK_INTERRUPTIBLE to capture signals */
        set_current_state(TASK_UNINTERRUPTIBLE);
        schedule_timeout(tj);
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
