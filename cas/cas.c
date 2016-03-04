/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Egor Nikulenkov <egor.nikulenkov@seagate.com>
 * Original creation date: 5-May-2016
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CAS

#include "lib/trace.h"
#include "fid/fid.h"         /* m0_fid_type_register */
#include "fop/fop.h"
#include "rpc/rpc_opcodes.h"
#include "cas/cas.h"
#include "cas/cas_xc.h"

struct m0_fom_type_ops;
struct m0_sm_conf;
struct m0_reqh_service_type;

/**
 * @addtogroup cas
 *
 * @{
 */

M0_INTERNAL struct m0_fop_type cas_get_fopt;
M0_INTERNAL struct m0_fop_type cas_put_fopt;
M0_INTERNAL struct m0_fop_type cas_del_fopt;
M0_INTERNAL struct m0_fop_type cas_cur_fopt;
M0_INTERNAL struct m0_fop_type cas_rep_fopt;

static int cas_fops_init(const struct m0_sm_conf           *sm_conf,
			 const struct m0_fom_type_ops      *fom_ops,
			 const struct m0_reqh_service_type *svctype)
{
	M0_FOP_TYPE_INIT(&cas_get_fopt,
			 .name      = "cas-get",
			 .opcode    = M0_CAS_GET_FOP_OPCODE,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
			 .xt        = m0_cas_op_xc,
			 .fom_ops   = fom_ops,
			 .sm        = sm_conf,
			 .svc_type  = svctype);
	M0_FOP_TYPE_INIT(&cas_put_fopt,
			 .name      = "cas-put",
			 .opcode    = M0_CAS_PUT_FOP_OPCODE,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
				      M0_RPC_ITEM_TYPE_MUTABO,
			 .xt        = m0_cas_op_xc,
			 .fom_ops   = fom_ops,
			 .sm        = sm_conf,
			 .svc_type  = svctype);
	M0_FOP_TYPE_INIT(&cas_del_fopt,
			 .name      = "cas-del",
			 .opcode    = M0_CAS_DEL_FOP_OPCODE,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
				      M0_RPC_ITEM_TYPE_MUTABO,
			 .xt        = m0_cas_op_xc,
			 .fom_ops   = fom_ops,
			 .sm        = sm_conf,
			 .svc_type  = svctype);
	M0_FOP_TYPE_INIT(&cas_cur_fopt,
			 .name      = "cas-cur",
			 .opcode    = M0_CAS_CUR_FOP_OPCODE,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
			 .xt        = m0_cas_op_xc,
			 .fom_ops   = fom_ops,
			 .sm        = sm_conf,
			 .svc_type  = svctype);
	M0_FOP_TYPE_INIT(&cas_rep_fopt,
			 .name      = "cas-rep",
			 .opcode    = M0_CAS_REP_FOP_OPCODE,
			 .rpc_flags = M0_RPC_ITEM_TYPE_REPLY,
			 .xt        = m0_cas_rep_xc,
			 .svc_type  = svctype);
	return  m0_fop_type_addb2_instrument(&cas_get_fopt) ?:
		m0_fop_type_addb2_instrument(&cas_put_fopt) ?:
		m0_fop_type_addb2_instrument(&cas_del_fopt) ?:
		m0_fop_type_addb2_instrument(&cas_cur_fopt);
}

static void cas_fops_fini(void)
{
	m0_fop_type_addb2_deinstrument(&cas_cur_fopt);
	m0_fop_type_addb2_deinstrument(&cas_del_fopt);
	m0_fop_type_addb2_deinstrument(&cas_put_fopt);
	m0_fop_type_addb2_deinstrument(&cas_get_fopt);
	m0_fop_type_fini(&cas_rep_fopt);
	m0_fop_type_fini(&cas_cur_fopt);
	m0_fop_type_fini(&cas_del_fopt);
	m0_fop_type_fini(&cas_put_fopt);
	m0_fop_type_fini(&cas_get_fopt);
}

/**
 * FID of the meta-index. It has the smallest possible FID in order to be always
 * the first during iteration over existing indices.
 */
M0_INTERNAL struct m0_fid m0_cas_meta_fid = M0_FID_TINIT('i', 0, 0);

M0_INTERNAL struct m0_fid_type m0_cas_index_fid_type = {
	.ft_id   = 'i',
	.ft_name = "cas-index"
};

M0_INTERNAL int m0_cas_module_init(void)
{
	struct m0_sm_conf            *sm_conf;
	const struct m0_fom_type_ops *fom_ops;
	struct m0_reqh_service_type  *svctype;

	m0_fid_type_register(&m0_cas_index_fid_type);
	m0_cas_svc_init();
	m0_cas_svc_fop_args(&sm_conf, &fom_ops, &svctype);
	return cas_fops_init(sm_conf, fom_ops, svctype);
}

M0_INTERNAL void m0_cas_module_fini(void)
{
	cas_fops_fini();
	m0_cas_svc_fini();
	m0_fid_type_unregister(&m0_cas_index_fid_type);
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of cas group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
