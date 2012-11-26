/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 05/05/2012
 */

#include "conf/conf_fop.h"
#include "conf/onwire_xc.h"
#include "fop/fop.h"
#include "fop/fop_item_type.h"
#include "fop/fom.h"
#include "fop/fom_generic.h"
#include "rpc/rpc.h"
#include "rpc/rpc_opcodes.h"
#include "lib/errno.h"
#include "lib/memory.h"

struct c2_fop_type c2_conf_fetch_fopt;
struct c2_fop_type c2_conf_fetch_resp_fopt;

struct c2_fop_type c2_conf_update_fopt;
struct c2_fop_type c2_conf_update_resp_fopt;

extern const struct c2_fom_type_ops c2_fom_conf_fetch_type_ops;
extern const struct c2_fom_type_ops c2_fom_conf_update_type_ops;

C2_INTERNAL int c2_conf_fops_init(void)
{
        return
		/* Fetch request/response */
		C2_FOP_TYPE_INIT(&c2_conf_fetch_fopt,
				 .name      = "c2_conf_fetch fop",
				 .opcode    = C2_CONF_FETCH_OPCODE,
				 .xt        = c2_conf_fetch_xc,
				 /* XXX FIXME Why setting MUTABO flag?  This fop
				  * does not change file system state.  (Search
				  * for MUTABO in rpc/slot_internal.h
				  * documentation.)  --vvv */
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
					      C2_RPC_ITEM_TYPE_MUTABO,
				 .fom_ops   = &c2_fom_conf_fetch_type_ops,
				 .sm        = &c2_generic_conf) ?:
		C2_FOP_TYPE_INIT(&c2_conf_fetch_resp_fopt,
				 .name      = "c2_conf_fetch_resp fop",
				 .opcode    = C2_CONF_FETCH_RESP_OPCODE,
				 .xt        = c2_conf_fetch_resp_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
		/* Update request/response */
		C2_FOP_TYPE_INIT(&c2_conf_update_fopt,
				 .name      = "c2_conf_update fop",
				 .opcode    = C2_CONF_UPDATE_OPCODE,
				 .xt        = c2_conf_update_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
					      C2_RPC_ITEM_TYPE_MUTABO,
				 .fom_ops   = &c2_fom_conf_update_type_ops,
				 .sm        = &c2_generic_conf) ?:
		C2_FOP_TYPE_INIT(&c2_conf_update_resp_fopt,
				 .name      = "c2_conf_update_resp fop",
				 .opcode    = C2_CONF_UPDATE_RESP_OPCODE,
				 .xt        = c2_conf_update_resp_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY);
}

C2_INTERNAL void c2_conf_fops_fini(void)
{
	c2_fop_type_fini(&c2_conf_fetch_fopt);
	c2_fop_type_fini(&c2_conf_fetch_resp_fopt);

	c2_fop_type_fini(&c2_conf_update_fopt);
	c2_fop_type_fini(&c2_conf_update_resp_fopt);
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
