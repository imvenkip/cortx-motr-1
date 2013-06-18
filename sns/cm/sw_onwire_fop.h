/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 06/07/2013
 */

#pragma once

#ifndef __MERO_SNS_CM_SW_ONWIRE_FOP_H__
#define __MERO_SNS_CM_SW_ONWIRE_FOP_H__

#include "xcode/xcode_attr.h"

#include "cm/sw.h"
#include "cm/sw_xc.h"

/**
   @defgroup SNSCMSW SNS sliding window
   @ingroup SNSCM

   @{
 */

struct m0_sns_cm_sw_onwire {
	struct m0_cm_sw_onwire swo_base;
}M0_XCA_RECORD;

M0_INTERNAL int m0_sns_cm_sw_onwire_fop_init(void);
M0_INTERNAL void m0_sns_cm_sw_onwire_fop_fini(void);

M0_INTERNAL int
m0_sns_cm_sw_onwire_fop_setup(struct m0_cm *cm, struct m0_fop *fop,
			      void (*fop_release)(struct m0_ref *),
			      const char *local_ep, const struct m0_cm_sw *sw);

extern struct m0_fop_type m0_sns_cm_sw_onwire_fopt;

/** @} SNSCMSW */

#endif /* __MERO_SNS_CM_SW_ONWIRE_FOP_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
