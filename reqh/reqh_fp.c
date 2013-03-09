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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 8-Mar-2013
 */

#include "lib/errno.h"
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "mero/magic.h"

/**
   @addtogroup reqh_fp
   @{
 */

M0_INTERNAL int m0_reqh_fp_init(struct m0_reqh_fop_policy *fp)
{
	fp->rfp_magic = M0_REQH_FP_MAGIC;
	return 0;
}

M0_INTERNAL bool m0_reqh_fp_is_initalized(const struct m0_reqh_fop_policy *fp)
{
	return false;
}

M0_INTERNAL void m0_reqh_fp_fini(struct m0_reqh_fop_policy *fp)
{
}

M0_INTERNAL int m0_reqh_fp_state_get(struct m0_reqh_fop_policy *fp)
{
	return 0;
}

M0_INTERNAL void m0_reqh_fp_state_set(struct m0_reqh_fop_policy *fp,
				      enum m0_reqh_fop_policy_states state)
{
}

M0_INTERNAL void m0_reqh_fp_mgmt_service_set(struct m0_reqh_fop_policy *fp,
					     struct m0_reqh_service *mgmt_svc)
{
}

M0_INTERNAL int m0_reqh_fp_accept(struct m0_reqh_fop_policy *fp,
				  struct m0_reqh *reqh,
				  struct m0_fop *fop)
{
	return -ENOSYS;
}

/** @} end reqh_fp group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
