/* -*- C -*- */

#include "lib/time.h"
#include "lib/assert.h"  /* C2_CASSERT */
#include "lib/cdefs.h"   /* C2_EXPORTED */
#include <linux/module.h>
#include <linux/time.h>

/**
   @addtogroup time

   <b>Implementation of c2_time on top of kernel struct timespec

   @{
*/

void c2_time_now(struct c2_time *time)
{
	C2_PRE(time != NULL);
        time->ts = current_kernel_time();
}
C2_EXPORTED(c2_time_now);

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
