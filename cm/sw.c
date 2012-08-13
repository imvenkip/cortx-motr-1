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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
                    Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 30/11/2011
 */

#include "lib/assert.h" /* C2_PRE(), C2_POST() */
#include "lib/trace.h"  /* C2_LOG() */

#include "cm/sw.h"
#include "cm/ag.h"

int c2_cm_sw_init(struct c2_cm_sw *sw, const struct c2_cm_sw_ops *sw_ops)
{
        C2_PRE(sw != NULL && sw_ops != NULL);
        C2_ENTRY();

        sw->sw_ops = sw_ops;
        aggr_grps_tlist_init(&sw->sw_aggr_grps);
        sw->sw_high = NULL;
        sw->sw_low = NULL;
        C2_LEAVE();
        return 0;
}

void c2_cm_sw_fini(struct c2_cm_sw *sw)
{
        C2_PRE(sw != NULL);
        C2_ENTRY();

        aggr_grps_tlist_fini(&sw->sw_aggr_grps);
        C2_LEAVE();
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

