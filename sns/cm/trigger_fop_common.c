/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Mikhail Antropov <mikhail.v.antropov@seagate.com>
 * Original creation date: 08/14/2015
 */

#include "fop/fop.h"
#include "fop/fop_item_type.h"

#include "sns/cm/cm.h"
#include "sns/cm/trigger_fop.h"
#include "sns/cm/trigger_fom.h"
#include "sns/cm/trigger_fop_xc.h"

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/trace.h"

/*
 * Implements a simplistic sns repair trigger FOM for corresponding trigger FOP.
 * This is solely for testing purpose and a separate trigger FOP/FOM will be
 * implemented later, which would be similar to this one.
 */

struct m0_fop_type m0_sns_repair_trigger_fopt;
struct m0_fop_type m0_sns_repair_quiesce_trigger_fopt;
struct m0_fop_type m0_sns_repair_status_fopt;
struct m0_fop_type m0_sns_rebalance_trigger_fopt;
struct m0_fop_type m0_sns_rebalance_quiesce_trigger_fopt;
struct m0_fop_type m0_sns_rebalance_status_fopt;
struct m0_fop_type m0_sns_repair_abort_fopt;

struct m0_fop_type m0_sns_repair_trigger_rep_fopt;
struct m0_fop_type m0_sns_repair_quiesce_trigger_rep_fopt;
struct m0_fop_type m0_sns_repair_status_rep_fopt;
struct m0_fop_type m0_sns_rebalance_trigger_rep_fopt;
struct m0_fop_type m0_sns_rebalance_quiesce_trigger_rep_fopt;
struct m0_fop_type m0_sns_rebalance_status_rep_fopt;
struct m0_fop_type m0_sns_repair_abort_rep_fopt;

#ifndef __KERNEL__
extern struct m0_sm_state_descr m0_sns_trigger_phases[];
extern const struct m0_fom_type_ops m0_sns_trigger_fom_type_ops;
extern const struct m0_sm_conf m0_sns_trigger_conf;
#endif

M0_INTERNAL void m0_sns_cm_trigger_fop_fini(struct m0_fop_type *ft)
{
	m0_fop_type_fini(ft);
}

M0_INTERNAL void m0_sns_cm_trigger_fop_init(struct m0_fop_type *ft,
					    enum M0_RPC_OPCODES op,
					    const char *name,
					    const struct m0_xcode_type *xt,
					    uint64_t rpc_flags,
					    struct m0_cm_type *cmt)
{
#ifndef __KERNEL__
	m0_sm_conf_extend(m0_generic_conf.scf_state, m0_sns_trigger_phases,
			  m0_generic_conf.scf_nr_states);
#endif

	M0_FOP_TYPE_INIT(ft,
			 .name      = name,
			 .opcode    = op,
			 .xt        = xt,
#ifndef __KERNEL__
			 .fom_ops   = &m0_sns_trigger_fom_type_ops,
			 .svc_type  = &cmt->ct_stype,
			 .sm        = &m0_sns_trigger_conf,
#endif
			 .rpc_flags = rpc_flags);
}

M0_INTERNAL int m0_sns_cm_trigger_fop_alloc(struct m0_rpc_machine  *mach,
					    uint32_t                op,
					    struct m0_fop         **fop)
{
	static struct m0_fop_type *sns_fop_type[] = {
		[SNS_REPAIR]           = &m0_sns_repair_trigger_fopt,
		[SNS_REPAIR_QUIESCE]   = &m0_sns_repair_quiesce_trigger_fopt,
		[SNS_REBALANCE]        = &m0_sns_rebalance_trigger_fopt,
		[SNS_REBALANCE_QUIESCE]= &m0_sns_rebalance_quiesce_trigger_fopt,
		[SNS_REPAIR_STATUS]    = &m0_sns_repair_status_fopt,
		[SNS_REBALANCE_STATUS] = &m0_sns_rebalance_status_fopt,
		[SNS_REPAIR_ABORT]     = &m0_sns_repair_abort_fopt,
	};
	M0_ENTRY();
	M0_PRE(IS_IN_ARRAY(op, sns_fop_type));

	*fop = m0_fop_alloc(sns_fop_type[op], NULL, mach);
	return *fop == NULL ? M0_ERR(-ENOMEM) : M0_RC(0);
}

#undef M0_TRACE_SUBSYSTEM
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
