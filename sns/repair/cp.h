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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 08/06/2012
 */

#pragma once

#ifndef __COLIBRI_SNS_REPAIR_CP_H__
#define __COLIBRI_SNS_REPAIR_CP_H__

#include "lib/ext.h"

#include "cm/cp.h"

/**
 * In addition to c2_cm_cp_phase, these phases can be used. Transition between
 * non-standard is handled by phase specific code and not by phase_next().
 * This also help identifying specific operation as follows:
 *
 * @code
 *
 * if (fom->fo_phase == CCP_READ && rcp->rc_phase == SRP_IO_WAIT)
 *	 This help to identify that wait was for read IO.
 *
 * @endcode
 *
 */
enum c2_sns_repair_phase {
        SRP_RESOURCE_WAIT = 1,
	SRP_EXTENT_LOCK_WAIT,
        SRP_IO_WAIT
};

struct c2_sns_repair_cp {
	enum c2_sns_repair_phase rc_phase;
	struct c2_cm_cp		 rc_cp;

        /** The gob fid which this data belongs to */
        struct c2_fid		 rc_gfid;

        /** The extent in gob, similar to offset in a file */
        struct c2_ext		 rc_gext;

        /**
         * The cob fid which this data belongs to.
         * - When this cp belongs to a storage-in agent, it is where
         *   it reads from.
         * - When this cp is finished processing by collecting agent,
         *   it is the cob for storage-out agent to write to.
         */
        struct c2_fid		 rc_cfid;

        /** The extent in cob */
        struct c2_ext		 rc_cext;
};

extern const struct c2_cm_cp_ops c2_sns_repair_cp_ops;

#endif /* __COLIBRI_SNS_REPAIR_CP_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */

