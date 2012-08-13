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
 * Original author: Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 06/21/2011
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fop/fop.h"
#include "reqh/reqh_fops_xc.h"
#include "rpc/item.h"
#include "rpc/rpc_opcodes.h"
#include "xcode/bufvec_xcode.h"

/**
   @addtogroup reqh
   @{
 */

struct c2_fop_type c2_reqh_error_rep_fopt;

void c2_reqh_fop_fini(void)
{
	c2_fop_type_fini(&c2_reqh_error_rep_fopt);
	c2_xc_reqh_fops_xc_fini();
}

int c2_reqh_fop_init(void)
{
	c2_xc_reqh_fops_xc_init();
	return C2_FOP_TYPE_INIT(&c2_reqh_error_rep_fopt,
				.name      = "Reqh error reply",
				.opcode    = C2_REQH_ERROR_REPLY_OPCODE,
				.xt        = c2_reqh_error_rep_xc,
				.rpc_flags = C2_RPC_ITEM_TYPE_REPLY);
}

/** @} endgroup reqh */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
