/* -*- C -*- */

#include "lib/time.h"
#include "lib/assert.h"  /* C2_CASSERT */
#include "lib/cdefs.h"   /* C2_EXPORTED */
#include <stddef.h>
#include <sys/time.h>    /* gettimeofday */


/**
   @addtogroup time

   Implementation of c2_time on top of userspace struct timespec

   @{
*/

void c2_time_now(struct c2_time *time)
{
        struct timeval tv;

        C2_PRE(time != NULL);
        /* We could use clock_gettime(CLOCK_REALTIME, time) for nanoseconds,
         but we would have to link librt... */
        gettimeofday(&tv, NULL);
        time->ts.tv_sec = tv.tv_sec;
        time->ts.tv_nsec = tv.tv_usec * 1000;
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
