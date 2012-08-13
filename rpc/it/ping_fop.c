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
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 07/07/2011
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "fop/fom.h"
#include "fop/fop.h"
#include "rpc/it/ping_fop_xc.h"
#include "rpc/it/ping_fop.h"
#include "rpc/it/ping_fom.h"
#include "lib/errno.h"
#include "rpc/rpc2.h"
#include "fop/fop_item_type.h"

struct c2_fop_type c2_fop_ping_fopt;
struct c2_fop_type c2_fop_ping_rep_fopt;

void c2_ping_fop_fini(void)
{
	c2_fop_type_fini(&c2_fop_ping_rep_fopt);
        c2_fop_type_fini(&c2_fop_ping_fopt);
	c2_xc_ping_fop_xc_fini();
}

extern struct c2_fom_type c2_fom_ping_mopt;

int c2_ping_fop_init(void)
{
	c2_fop_ping_fopt.ft_fom_type = c2_fom_ping_mopt;
	c2_xc_ping_fop_xc_init();
        return  C2_FOP_TYPE_INIT(&c2_fop_ping_fopt,
				 .name      = "Ping fop",
				 .opcode    = C2_RPC_PING_OPCODE,
				 .xt        = c2_fop_ping_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
					      C2_RPC_ITEM_TYPE_MUTABO,
				 .fom_ops   = c2_fom_ping_mopt.ft_ops) ?:
		C2_FOP_TYPE_INIT(&c2_fop_ping_rep_fopt,
				 .name      = "Ping fop reply",
				 .opcode    = C2_RPC_PING_REPLY_OPCODE,
				 .xt        = c2_fop_ping_rep_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY);
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
