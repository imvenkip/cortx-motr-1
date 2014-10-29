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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 16/04/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/trace.h"
#include "lib/memory.h"

#include "fop/fop.h"
#include "pool/pool.h"
#include "reqh/reqh.h"

#include "sns/sns_addb.h"
#include "sns/cm/cm_utils.h"
#include "sns/cm/iter.h"
#include "sns/cm/cm.h"
#include "sns/cm/cp.h"
#include "sns/cm/file.h"
#include "sns/cm/rebalance/ag.h"
#include "sns/cm/sw_onwire_fop.h"

extern const struct m0_sns_cm_helpers rebalance_helpers;
extern const struct m0_cm_cp_ops m0_sns_cm_rebalance_cp_ops;

M0_INTERNAL int
m0_sns_cm_rebalance_sw_onwire_fop_setup(struct m0_cm *cm, struct m0_fop *fop,
					void (*fop_release)(struct m0_ref *),
					const char *local_ep,
					const struct m0_cm_sw *sw);


static struct m0_cm_cp *rebalance_cm_cp_alloc(struct m0_cm *cm)
{
	struct m0_sns_cm_cp *scp;

	SNS_ALLOC_PTR(scp, &m0_sns_mod_addb_ctx, CP_ALLOC);
	if (scp == NULL)
		return NULL;
	scp->sc_base.c_ops = &m0_sns_cm_rebalance_cp_ops;

	return &scp->sc_base;
}

static int rebalance_cm_prepare(struct m0_cm *cm)
{
	struct m0_sns_cm *scm = cm2sns(cm);

	M0_ENTRY("cm: %p", cm);
	M0_PRE(scm->sc_op == SNS_REBALANCE);

	scm->sc_helpers = &rebalance_helpers;
	return m0_sns_cm_prepare(cm);

}

static int rebalance_cm_stop(struct m0_cm *cm)
{
	struct m0_sns_cm *scm = cm2sns(cm);

	M0_ASSERT(scm->sc_op == SNS_REBALANCE);

	return m0_sns_cm_stop(cm);
}

/**
 * Returns true iff the copy machine has enough space to receive all
 * the copy packets from the given relevant group "id".
 * Reserves buffers from incoming buffer pool struct m0_sns_cm::sc_ibp
 * corresponding to all the incoming copy packets.
 * e.g. sns repair copy machine checks if the incoming buffer pool has
 * enough free buffers to receive all the remote units corresponding
 * to a parity group.
 */
static bool rebalance_cm_has_space(struct m0_cm *cm,
				   const struct m0_cm_ag_id *id,
				   struct m0_layout *l)
{
	struct m0_sns_cm          *scm = cm2sns(cm);
	struct m0_pdclust_layout  *pl  = m0_layout_to_pdl(l);
	struct m0_fid              fid;
	struct m0_sns_cm_file_ctx *fctx;
	uint64_t                   tot_inbufs;

	M0_PRE(cm != NULL && id != NULL && l != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	agid2fid(id, &fid);
	m0_mutex_lock(&scm->sc_file_ctx_mutex);
	fctx = m0_sns_cm_fctx_locate(scm, &fid);
	m0_mutex_unlock(&scm->sc_file_ctx_mutex);
	M0_ASSERT(fctx != NULL);
	tot_inbufs = m0_sns_cm_incoming_reserve_bufs(scm, id, pl, fctx->sf_pi);
	return m0_sns_cm_has_space_for(scm, pl, tot_inbufs);
}

/** Copy machine operations. */
const struct m0_cm_ops sns_rebalance_ops = {
	.cmo_setup               = m0_sns_cm_setup,
	.cmo_prepare             = rebalance_cm_prepare,
	.cmo_start               = m0_sns_cm_start,
	.cmo_ag_alloc            = m0_sns_cm_rebalance_ag_alloc,
	.cmo_cp_alloc            = rebalance_cm_cp_alloc,
	.cmo_data_next           = m0_sns_cm_iter_next,
	.cmo_ag_next             = m0_sns_cm_ag_next,
	.cmo_has_space           = rebalance_cm_has_space,
	.cmo_sw_onwire_fop_setup = m0_sns_cm_rebalance_sw_onwire_fop_setup,
	.cmo_stop                = rebalance_cm_stop,
	.cmo_fini                = m0_sns_cm_fini
};

#undef M0_TRACE_SUBSYSTEM

/** @} SNSCM */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
