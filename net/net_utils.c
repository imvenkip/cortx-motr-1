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
 * Original author: Nathan Rutman <Nathan_Rutman@us.xyratex.com>,
 *                  Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 06/15/2010
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifndef __KERNEL__
#include "lib/misc.h"
#endif

#include "lib/arith.h"
#include "lib/misc.h"   /* C2_SET0 */
#include "net/net.h"


/**
   @addtogroup netDep Networking (Deprecated Interfaces)
 */

bool c2_services_are_same(const struct c2_service_id *c1,
			  const struct c2_service_id *c2)
{
	return memcmp(c1, c2, sizeof *c1) == 0;
}

void c2_net_domain_stats_init(struct c2_net_domain *dom)
{
        c2_time_t now;
        int i;

        C2_SET0(&dom->nd_stats);

        now = c2_time_now();
        for (i = 0; i < ARRAY_SIZE(dom->nd_stats); i++) {
                c2_rwlock_init(&dom->nd_stats[i].ns_lock);
                dom->nd_stats[i].ns_time = now;
                /** @todo we could add a provision for storing max persistently
                 */
                /* Lie to avoid division by 0 */
                dom->nd_stats[i].ns_max = 1;
                c2_atomic64_set(&dom->nd_stats[i].ns_threads_woken, 1);
                c2_atomic64_set(&dom->nd_stats[i].ns_reqs, 1);
        }
}

void c2_net_domain_stats_fini(struct c2_net_domain *dom)
{
        int i;

        for (i = 0; i < ARRAY_SIZE(dom->nd_stats); i++) {
                c2_rwlock_fini(&dom->nd_stats[i].ns_lock);
        }
}

/**
 Collect some network loading stats

 @todo fm_sizeof used in calls to here gives the size of a "top-most"
 in-memory fop struct.  All substructures pointed to from the
 top are allocated deep inside XDR bowels. E.g., for read or
 write this won't include data buffers size.
 Either we find a way to extract the total size from sunrpc
 or we should write a generic fop-type function traversing
 fop-format tree.
 Note that even if not accturate, if the number reported is
 reflective of the actual rate that is sufficient for relative
 loading estimation.  In fact, # reqs may be a sufficient
 proxy for bytes. */
void c2_net_domain_stats_collect(struct c2_net_domain *dom,
                                 enum c2_net_stats_direction dir,
                                 uint64_t bytes,
                                 bool *sleeping)
{
        c2_atomic64_inc(&dom->nd_stats[dir].ns_reqs);
        c2_atomic64_add(&dom->nd_stats[dir].ns_bytes, bytes);
        if (*sleeping) {
                c2_atomic64_inc(&dom->nd_stats[dir].ns_threads_woken);
                *sleeping = false;
        }
}

/**
  Report the network loading rate for a direction (in/out).
  Assume semi-regular calling of this function; timebase is simply the time
  between calls.
  @returns rate, in percent * 100 of maximum seen rate (e.g. 1234 = 12.34%)
 */
int c2_net_domain_stats_get(struct c2_net_domain *dom,
                            enum c2_net_stats_direction dir)
{
        uint64_t interval_usec;
        uint64_t rate;
        uint64_t max;
        c2_time_t now;
        c2_time_t interval;
        int rv;

        now = c2_time_now();

        /* We lock here against other callers only -- stats are still being
           collected while we are here. We reset stats only after we calculate
           the rate, and lock so that the rate calculation and reset is atomic.
           The reported rate may be slightly off because of the ongoing
           collection. */
        c2_rwlock_write_lock(&dom->nd_stats[dir].ns_lock);

        interval = c2_time_sub(now, dom->nd_stats[dir].ns_time);
        interval_usec = interval;
        interval_usec = max64u(interval_usec, 1);

        /* Load based on data rate only, bytes/sec */
        rate = c2_atomic64_get(&dom->nd_stats[dir].ns_bytes) *
                          C2_TIME_ONE_BILLION / interval_usec;
        max = max64u(dom->nd_stats[dir].ns_max, rate);
        rv = (int)((rate * 10000) / max);

        /* At start of world we might think any data rate is the max. Instead
           we can use threads_woken == reqs to calculate if we're not busy --
           if the req queues don't build up, there is probably more capacity.
           The converse, high queue depth implies no more network capacity,
           is not true, since queues may be limited by some other resource. */
        if (!dom->nd_stats[dir].ns_got_busy) {
                if (c2_atomic64_get(&dom->nd_stats[dir].ns_threads_woken) << 8 /
                    c2_atomic64_get(&dom->nd_stats[dir].ns_reqs) > 230)
                        /* at least 90% not busy, report "10% busy" */
                        rv = (10 * 10000) / 100;
                else
                        /* Once we get a little busy, start trusting max */
                        dom->nd_stats[dir].ns_got_busy = true;
        }

        /* Reset stats */
        dom->nd_stats[dir].ns_time = now;
        c2_atomic64_set(&dom->nd_stats[dir].ns_bytes, 0);
        /* Lie to avoid division by 0 */
        c2_atomic64_set(&dom->nd_stats[dir].ns_threads_woken, 1);
        c2_atomic64_set(&dom->nd_stats[dir].ns_reqs, 1);
        dom->nd_stats[dir].ns_max = max;

        c2_rwlock_write_unlock(&dom->nd_stats[dir].ns_lock);

        return rv;
}



/** @} end of net group */
