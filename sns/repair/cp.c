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

#include "sns/repair/cp.h"

/**
   @addtogroup snsrepair

   @{
*/

static int cp_init(struct c2_cm_cp *cp)
{
	return 0;
}

static void cp_fini(struct c2_cm_cp *cp)
{
}


static int cp_state(struct c2_cm_cp *cp)
{
	return 0;
}

static void cp_complete(struct c2_cm_cp *cp)
{
}

static const struct c2_cm_cp_ops cp_ops = {
	.co_init     = &cp_init,
	.co_fini     = &cp_fini,
	.co_read     = NULL,
	.co_write    = NULL,
	.co_send     = NULL,
	.co_recv     = NULL,
	.co_xform    = NULL,
	.co_state    = &cp_state,
	.co_complete = &cp_complete
};

/** @} snsrepair */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */

