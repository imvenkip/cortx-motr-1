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
   @defgroup SNSRepairCP SNS Repair Copy packet
   @ingroup SNSRepairCM

 */

/**
 * In addition to c2_cm_cp_phase, these phases can be used. Transition between
 * non-standard phase handled by phase specific code and not by next phase
 * function (co_phase()). This also helps identifying specific operation as
 * follows:
 *
 * @code
 *
 * if (fom->fo_phase == C2_CCP_READ && rcp->rc_phase == SRP_IO_WAIT)
 *	 This helps to identify that wait was for read IO.
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
	struct c2_cm_cp		 rc_base;

	/** SNS copy packet specific phases.*/
	enum c2_sns_repair_phase rc_phase;

        /** The gob fid which this data belongs to. */
        struct c2_fid		 rc_gfid;

        /** The extent in gob, similar to offset in a file. */
        struct c2_ext		 rc_gext;

        /**
         * The cob fid which this data belongs to.
         * - In READ phase, it is where it reads from.
         * - In WRITE phase, it is where it write to.
         */
        struct c2_fid		 rc_cfid;

        /** The extent in cob. */
        struct c2_ext		 rc_cext;

        /** Set and used in case of read/write.*/
        struct c2_stob_id	 rc_sid;
};

extern const struct c2_cm_cp_ops c2_sns_repair_cp_ops;

/** @} SNSRepairCP */
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

