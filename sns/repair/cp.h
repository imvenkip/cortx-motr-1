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
 * Original creation date: 16/04/2012
 */

#ifndef __COLIBRI_SNS_REPAIR_CP_H__
#define __COLIBRI_SNS_REPAIR_CP_H__

#include "cm/cp.h"

/**
   @addtogroup snsrepair

   @{
*/

struct c2_repair_cp {
	struct c2_cm_cp rc_base;

        /** The gob fid which this data belongs to */
        struct c2_fid   rc_gfid;

        /** The extent in gob, similar to offset in a file */
        struct c2_ext   rc_gext;

        /**
         * The cob fid which this data belongs to.
         * - When this cp belongs to a storage-in agent, it is where
         *   it reads from.
         * - When this cp is finished processing by collecting agent,
         *   it is the cob for storage-out agent to write to.
         */
        struct c2_fid   rc_cfid;

        /** The extent in cob */
        struct c2_ext   rc_cext;
};

void c2_repair_cp_init(struct c2_repair_cp *scp,
		       const struct c2_fid *gfid,
		       const struct c2_ext *gext);

void c2_repair_cp_fini(struct c2_repair_cp *scp);

struct c2_repair_cp *c2_repair_cp_get(struct c2_cm *cm);

/** @} snsrepair */
/* __COLIBRI_SNS_REPAIR_H__ */

#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
