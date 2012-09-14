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
 *                  Subhash Arya  <subhash_arya@xyratex.com>
 * Original creation date: 11/11/2011
 */

#pragma once

#ifndef __COLIBRI_CM_SW_H__
#define __COLIBRI_CM_SW_H__

#include "lib/tlist.h"  /* struct c2_tlink */

/**
   @defgroup CMSW Copy machine sliding window
   @ingroup CM
   @{

 */

/**
 * While copy machine is processing a restructuring request, each replica
 * maintains a "sliding window" (SW), indicating how far it gets. This window is
 * a pair of group identifiers [LO, HI], with LO <= HI. The following invariant
 * is maintained:
 *
 * - the replica already completely processed all groups it had to process
 *   with identifiers less than LO;
 *
 * - the replica is ready to accept copy packets for groups with identifiers ID
 *   such that LO <= ID <= HI.
 */
struct c2_cm_sw {
        /** Sliding window operations. */
        const struct c2_cm_sw_ops  *sw_ops;

        /** List of aggregation groups being processed by the copy machine.*/
        struct c2_tl                sw_aggr_grps;
};

/** Copy Machine sliding window operations. */
struct c2_cm_sw_ops {
	
};

int c2_cm_sw_update(struct c2_cm_sw *slw

/** Initialises sliding window. */
int c2_cm_sw_init(struct c2_cm_sw *slw, const struct c2_cm_sw_ops *ops);

/** Finalises sliding window. */
void c2_cm_sw_fini(struct c2_cm_sw *slw);

/** @} CMSW */
#endif /* __COLIBRI_CM_SW_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

