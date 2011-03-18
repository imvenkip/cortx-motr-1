/* -*- C -*- */

#include "lib/time.h"
#include "lib/assert.h"  /* C2_CASSERT */
#include "lib/cdefs.h"   /* C2_EXPORTED */
#include <stddef.h>

extern int nanosleep(const struct timespec *req, struct timespec *rem);

/**
   @addtogroup time

   Implementation of c2_time on top of userspace struct timespec

   @{
*/

struct c2_time *c2_time_now(struct c2_time *time)
{
        struct timeval tv;

        C2_PRE(time != NULL);
        /* We could use clock_gettime(CLOCK_REALTIME, time) for nanoseconds,
         but we would have to link librt... */
        gettimeofday(&tv, NULL);
        time->ts.tv_sec = tv.tv_sec;
        time->ts.tv_nsec = tv.tv_usec * 1000;
	return time;
}
C2_EXPORTED(c2_time_now);

/**
   Sleep for requested time
*/
int c2_nanosleep(const struct c2_time *req, struct c2_time *rem)
{
	struct timespec remaining = req->ts;
	int rc;

	rc = nanosleep(&req->ts, &remaining);
	if (rem != NULL)
		rem->ts = remaining;
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
