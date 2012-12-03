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
#include "conf/confd_fom.h"   /* c2_confd_fom_create */
#include "conf/confd.h"       /* c2_confd_stype */
#include "rpc/rpc_opcodes.h"
#include "fop/fom_generic.h"  /* c2_generic_conf */

/**
 * @addtogroup conf_fop
 *
 * @{
 */

struct c2_fop_type c2_conf_fetch_fopt;
struct c2_fop_type c2_conf_fetch_resp_fopt;

struct c2_fop_type c2_conf_update_fopt;
struct c2_fop_type c2_conf_update_resp_fopt;

#ifndef __KERNEL__
static const struct c2_fom_type_ops confd_fom_ops = {
	.fto_create = c2_confd_fom_create
};
#endif

C2_INTERNAL int c2_conf_fops_init(void)
{
        return  C2_FOP_TYPE_INIT(&c2_conf_fetch_fopt,
				 .name      = "Configuration fetch request",
				 .opcode    = C2_CONF_FETCH_OPCODE,
				 .xt        = c2_conf_fetch_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST,
#ifndef __KERNEL__
				 .fom_ops   = &confd_fom_ops,
				 .svc_type  = &c2_confd_stype,
#endif
				 .sm        = &c2_generic_conf) ?:
		C2_FOP_TYPE_INIT(&c2_conf_fetch_resp_fopt,
				 .name      = "Configuration fetch response",
				 .opcode    = C2_CONF_FETCH_RESP_OPCODE,
				 .xt        = c2_conf_fetch_resp_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
		/*
		 * XXX Argh! Why bother defining update _stubs_?
		 * Do we win anything? Is it worth the cost of maintenance?
		 */
		C2_FOP_TYPE_INIT(&c2_conf_update_fopt,
				 .name      = "Configuration update request",
				 .opcode    = C2_CONF_UPDATE_OPCODE,
				 .xt        = c2_conf_update_xc,
				 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
					      C2_RPC_ITEM_TYPE_MUTABO,
#ifndef __KERNEL__
				 .fom_ops   = &confd_fom_ops,
				 .svc_type  = &c2_confd_stype,
#endif
				 .sm        = &c2_generic_conf) ?:
		C2_FOP_TYPE_INIT(&c2_conf_update_resp_fopt,
				 .name      = "Configuration update response",
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

/** @} conf_fop */
