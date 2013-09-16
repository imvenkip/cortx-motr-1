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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 *                  Mandar Sawant <mandar_sawant@xyratex.com>
 *                  Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 08/06/2012
 */

#ifndef M0_TRACE_SUBSYSTEM
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#endif
#include "lib/memory.h" /* m0_free() */
#include "lib/misc.h"

#include "cob/cob.h"
#include "fop/fom.h"
#include "reqh/reqh.h"
#include "sns/sns_addb.h"
#include "sns/cm/cp.h"
#include "sns/cm/cm.h"
#include "sns/cm/ag.h"
#include "sns/cm/sns_cp_onwire.h"

/**
  @addtogroup SNSCMCP

  @{
*/

M0_INTERNAL int m0_sns_cm_cob_is_local(struct m0_fid *cobfid,
				       struct m0_cob_domain *cdom);

M0_INTERNAL int m0_sns_cm_repair_cp_xform(struct m0_cm_cp *cp);
M0_INTERNAL int m0_sns_cm_rebalance_cp_xform(struct m0_cm_cp *cp);

M0_INTERNAL int m0_sns_cm_repair_cp_send(struct m0_cm_cp *cp);
M0_INTERNAL int m0_sns_cm_rebalance_cp_send(struct m0_cm_cp *cp);

M0_INTERNAL int m0_sns_cm_repair_cp_recv_wait(struct m0_cm_cp *cp);
M0_INTERNAL int m0_sns_cm_rebalance_cp_recv_wait(struct m0_cm_cp *cp);

M0_INTERNAL void m0_sns_cm_cp_addb_log(const struct m0_cm_cp *cp)
{
	struct m0_cm_aggr_group *ag;
	struct m0_sns_cm_ag     *sns_ag;
	struct m0_sns_cm_cp     *sns_cp;

	sns_cp = cp2snscp(cp);
	ag = cp->c_ag;
	sns_ag = ag2snsag(ag);

	M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_sns_ag_info,
		     M0_ADDB_CTX_VEC(&m0_sns_ag_addb_ctx),
		     ag->cag_id.ai_hi.u_hi, ag->cag_id.ai_hi.u_lo,
		     ag->cag_id.ai_lo.u_hi, ag->cag_id.ai_lo.u_lo,
		     ag->cag_cp_local_nr, ag->cag_cp_global_nr,
		     ag->cag_transformed_cp_nr, ag->cag_has_incoming,
		     sns_ag->sag_fnr);

	M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_sns_cp_info,
		     M0_ADDB_CTX_VEC(&m0_sns_cp_addb_ctx),
		     ag->cag_id.ai_hi.u_hi, ag->cag_id.ai_hi.u_lo,
                     ag->cag_id.ai_lo.u_hi, ag->cag_id.ai_lo.u_lo,
		     sns_cp->sc_sid.si_bits.u_hi, sns_cp->sc_sid.si_bits.u_lo,
		     sns_cp->sc_index, sns_cp->sc_is_local);
}

M0_INTERNAL struct m0_sns_cm_cp *cp2snscp(const struct m0_cm_cp *cp)
{
	return container_of(cp, struct m0_sns_cm_cp, sc_base);
}

M0_INTERNAL bool m0_sns_cm_cp_invariant(const struct m0_cm_cp *cp)
{
	struct m0_sns_cm_cp *sns_cp = cp2snscp(cp);

	return m0_fom_phase(&cp->c_fom) < M0_CCP_NR &&
	       ergo(m0_fom_phase(&cp->c_fom) > M0_CCP_INIT,
		    m0_stob_id_is_set(&sns_cp->sc_sid));
}

/*
 * Uses GOB fid key and parity group number to generate a scalar to
 * help select a request handler locality for copy packet FOM.
 */
M0_INTERNAL uint64_t cp_home_loc_helper(const struct m0_cm_cp *cp)
{
	struct m0_sns_cm_cp *sns_cp = cp2snscp(cp);
	struct m0_fop       *fop = cp->c_fom.fo_fop;
	struct m0_sns_cpx   *sns_cpx;

	/*
         * Serialize read on a particular stob by returning target
         * container id to assign a reqh locality to the cp fom.
         */
	if (fop != NULL && (m0_fom_phase(&cp->c_fom) != M0_CCP_FINI)) {
		sns_cpx = m0_fop_data(fop);
		return sns_cpx->scx_sid.f_container;
	} else
		return sns_cp->sc_cobfid.f_container;
}

M0_INTERNAL int m0_sns_cm_cp_init(struct m0_cm_cp *cp)
{
	struct m0_sns_cpx *sns_cpx;

	M0_PRE(m0_fom_phase(&cp->c_fom) == M0_CCP_INIT);

	if (cp->c_fom.fo_fop != NULL) {
		sns_cpx = m0_fop_data(cp->c_fom.fo_fop);
		m0_fom_phase_set(&cp->c_fom, sns_cpx->scx_phase);
	}
	return cp->c_ops->co_phase_next(cp);
}

static int next[] = {
	[M0_CCP_INIT]        = M0_CCP_READ,
	[M0_CCP_READ]        = M0_CCP_IO_WAIT,
	[M0_CCP_XFORM]       = M0_CCP_WRITE,
	[M0_CCP_WRITE]       = M0_CCP_IO_WAIT,
	[M0_CCP_IO_WAIT]     = M0_CCP_XFORM,
	[M0_CCP_SW_CHECK]    = M0_CCP_SEND,
	[M0_CCP_SEND]        = M0_CCP_RECV_INIT,
	[M0_CCP_SEND_WAIT]   = M0_CCP_FINI,
	[M0_CCP_RECV_INIT]   = M0_CCP_RECV_WAIT,
	[M0_CCP_RECV_WAIT]   = M0_CCP_XFORM
};

M0_INTERNAL int m0_sns_cm_cp_phase_next(struct m0_cm_cp *cp)
{
	int phase = m0_sns_cm_cp_next_phase_get(m0_fom_phase(&cp->c_fom), cp);

	m0_fom_phase_set(&cp->c_fom, phase);

        return M0_IN(phase, (M0_CCP_IO_WAIT, M0_CCP_SEND_WAIT,
			     M0_CCP_RECV_WAIT, M0_CCP_FINI)) ?
	       M0_FSO_WAIT : M0_FSO_AGAIN;
}

M0_INTERNAL int m0_sns_cm_cp_next_phase_get(int phase, struct m0_cm_cp *cp)
{
        struct m0_sns_cm     *scm;
	struct m0_sns_cm_cp  *scp = cp2snscp(cp);
        struct m0_cob_domain *cdom;
        int                   rc;

	M0_PRE(phase >= M0_CCP_INIT && phase < M0_CCP_NR);

	if (phase == M0_CCP_IO_WAIT) {
		if (cp->c_io_op == M0_CM_CP_WRITE)
			return M0_CCP_FINI;
	}

	if ((phase == M0_CCP_INIT && scp->sc_is_acc) || phase == M0_CCP_XFORM) {
		scm = cm2sns(cp->c_ag->cag_cm);
		cdom  = scm->sc_it.si_cob_dom;
		rc = m0_sns_cm_cob_is_local(&scp->sc_cobfid, cdom);
		if (rc == 0)
			return M0_CCP_WRITE;
		else if (rc == -ENOENT)
			return M0_CCP_SW_CHECK;
		else
			return M0_CCP_FINI;
	}

	return next[phase];
}

M0_INTERNAL void m0_sns_cm_cp_complete(struct m0_cm_cp *cp)
{
}

M0_INTERNAL void m0_sns_cm_cp_free(struct m0_cm_cp *cp)
{
	M0_PRE(cp != NULL);

	m0_cm_cp_buf_release(cp);
	m0_free(cp2snscp(cp));
}

/*
 * Dummy dud destructor function for struct m0_cm_cp_ops::co_action array
 * in-order to statisfy the m0_cm_cp_invariant.
 */
M0_INTERNAL int m0_sns_cm_cp_fini(struct m0_cm_cp *cp)
{
	return 0;
}

M0_INTERNAL void m0_sns_cm_cp_tgt_info_fill(struct m0_sns_cm_cp *scp,
					    const struct m0_fid *cob_fid,
					    uint64_t stob_offset,
					    uint64_t ag_cp_idx)
{
	scp->sc_cobfid = *cob_fid;
	scp->sc_sid.si_bits.u_hi = cob_fid->f_container;
	scp->sc_sid.si_bits.u_lo = cob_fid->f_key;
	scp->sc_index = stob_offset;
	scp->sc_base.c_ag_cp_idx = ag_cp_idx;
}

M0_INTERNAL int m0_sns_cm_cp_setup(struct m0_sns_cm_cp *scp,
				   const struct m0_fid *cob_fid,
				   uint64_t stob_offset,
				   uint64_t data_seg_nr,
				   uint64_t ag_cp_idx)
{
	struct m0_sns_cm *scm = cm2sns(scp->sc_base.c_ag->cag_cm);
	struct m0_net_buffer_pool *bp;

	M0_PRE(scp != NULL && scp->sc_base.c_ag != NULL);

	scp->sc_base.c_data_seg_nr = data_seg_nr;
	m0_sns_cm_cp_tgt_info_fill(scp, cob_fid, stob_offset, ag_cp_idx);
	m0_bitmap_init(&scp->sc_base.c_xform_cp_indices,
                       scp->sc_base.c_ag->cag_cp_global_nr);

	/*
	 * Set the bit value of own index if it is not an accumulator copy
	 * packet.
	 */
	if (ag_cp_idx != ~0)
		m0_bitmap_set(&scp->sc_base.c_xform_cp_indices, ag_cp_idx,
			      true);

	bp = scp->sc_is_local ? &scm->sc_obp.sb_bp : &scm->sc_ibp.sb_bp;

	return m0_sns_cm_buf_attach(bp, &scp->sc_base);
}

const struct m0_cm_cp_ops m0_sns_cm_repair_cp_ops = {
	.co_action = {
		[M0_CCP_INIT]         = &m0_sns_cm_cp_init,
		[M0_CCP_READ]         = &m0_sns_cm_cp_read,
		[M0_CCP_WRITE]        = &m0_sns_cm_cp_write,
		[M0_CCP_IO_WAIT]      = &m0_sns_cm_cp_io_wait,
		[M0_CCP_XFORM]        = &m0_sns_cm_repair_cp_xform,
		[M0_CCP_SW_CHECK]     = &m0_sns_cm_cp_sw_check,
		[M0_CCP_SEND]         = &m0_sns_cm_repair_cp_send,
		[M0_CCP_SEND_WAIT]    = &m0_sns_cm_cp_send_wait,
		[M0_CCP_RECV_INIT]    = &m0_sns_cm_cp_recv_init,
		[M0_CCP_RECV_WAIT]    = &m0_sns_cm_repair_cp_recv_wait,
		/* To satisfy the m0_cm_cp_invariant() */
		[M0_CCP_FINI]         = &m0_sns_cm_cp_fini,
	},
	.co_action_nr            = M0_CCP_NR,
	.co_phase_next	         = &m0_sns_cm_cp_phase_next,
	.co_invariant	         = &m0_sns_cm_cp_invariant,
	.co_home_loc_helper      = &cp_home_loc_helper,
	.co_complete	         = &m0_sns_cm_cp_complete,
	.co_free                 = &m0_sns_cm_cp_free,
};

const struct m0_cm_cp_ops m0_sns_cm_rebalance_cp_ops = {
	.co_action = {
		[M0_CCP_INIT]         = &m0_sns_cm_cp_init,
		[M0_CCP_READ]         = &m0_sns_cm_cp_read,
		[M0_CCP_WRITE]        = &m0_sns_cm_cp_write,
		[M0_CCP_IO_WAIT]      = &m0_sns_cm_cp_io_wait,
		[M0_CCP_XFORM]        = &m0_sns_cm_rebalance_cp_xform,
		[M0_CCP_SW_CHECK]     = &m0_sns_cm_cp_sw_check,
		[M0_CCP_SEND]         = &m0_sns_cm_rebalance_cp_send,
		[M0_CCP_SEND_WAIT]    = &m0_sns_cm_cp_send_wait,
		[M0_CCP_RECV_INIT]    = &m0_sns_cm_cp_recv_init,
		[M0_CCP_RECV_WAIT]    = &m0_sns_cm_rebalance_cp_recv_wait,
		/* To satisfy the m0_cm_cp_invariant() */
		[M0_CCP_FINI]         = &m0_sns_cm_cp_fini,
	},
	.co_action_nr            = M0_CCP_NR,
	.co_phase_next	         = &m0_sns_cm_cp_phase_next,
	.co_invariant	         = &m0_sns_cm_cp_invariant,
	.co_home_loc_helper      = &cp_home_loc_helper,
	.co_complete	         = &m0_sns_cm_cp_complete,
	.co_free                 = &m0_sns_cm_cp_free,
};

/** @} SNSCMCP */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
