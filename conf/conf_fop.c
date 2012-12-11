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
#include "conf/confd_fom.h"   /* m0_confd_fom_create */
#include "conf/confd.h"       /* m0_confd_stype */
#include "rpc/rpc_opcodes.h"
#include "fop/fom_generic.h"  /* m0_generic_conf */

/**
 * @addtogroup conf_fop
 *
 * @{
 */

struct m0_fop_type m0_conf_fetch_fopt;
struct m0_fop_type m0_conf_fetch_resp_fopt;

struct m0_fop_type m0_conf_update_fopt;
struct m0_fop_type m0_conf_update_resp_fopt;

#ifndef __KERNEL__
static const struct m0_fom_type_ops confd_fom_ops = {
	.fto_create = m0_confd_fom_create
};
#endif

M0_INTERNAL int m0_conf_fops_init(void)
{
        return  M0_FOP_TYPE_INIT(&m0_conf_fetch_fopt,
				 .name      = "Configuration fetch request",
				 .opcode    = M0_CONF_FETCH_OPCODE,
				 .xt        = m0_conf_fetch_xc,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
#ifndef __KERNEL__
				 .fom_ops   = &confd_fom_ops,
				 .svc_type  = &m0_confd_stype,
#endif
				 .sm        = &m0_generic_conf) ?:
		M0_FOP_TYPE_INIT(&m0_conf_fetch_resp_fopt,
				 .name      = "Configuration fetch response",
				 .opcode    = M0_CONF_FETCH_RESP_OPCODE,
				 .xt        = m0_conf_fetch_resp_xc,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY) ?:
		/*
		 * XXX Argh! Why bother defining update _stubs_?
		 * Do we win anything? Is it worth the cost of maintenance?
		 */
		M0_FOP_TYPE_INIT(&m0_conf_update_fopt,
				 .name      = "Configuration update request",
				 .opcode    = M0_CONF_UPDATE_OPCODE,
				 .xt        = m0_conf_update_xc,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
					      M0_RPC_ITEM_TYPE_MUTABO,
#ifndef __KERNEL__
				 .fom_ops   = &confd_fom_ops,
				 .svc_type  = &m0_confd_stype,
#endif
				 .sm        = &m0_generic_conf) ?:
		M0_FOP_TYPE_INIT(&m0_conf_update_resp_fopt,
				 .name      = "Configuration update response",
				 .opcode    = M0_CONF_UPDATE_RESP_OPCODE,
				 .xt        = m0_conf_update_resp_xc,
				 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
}

M0_INTERNAL void m0_conf_fops_fini(void)
{
	m0_fop_type_fini(&m0_conf_fetch_fopt);
	m0_fop_type_fini(&m0_conf_fetch_resp_fopt);

	m0_fop_type_fini(&m0_conf_update_fopt);
	m0_fop_type_fini(&m0_conf_update_resp_fopt);
}

/** @} conf_fop */
