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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 12/09/2012
 * Revision: Anup Barve <anup_barve@xyratex.com>
 * Revision date: 08/05/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/trace.h"

#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"

#include "fid/fid.h"
#include "sns/parity_repair.h"

#include "sns/sns_addb.h"
#include "sns/cm/cm_utils.h"
#include "sns/cm/repair/ag.h"
#include "sns/cm/cp.h"
#include "sns/cm/cm.h"

/**
   @addtogroup SNSCMAG

   @{
 */

M0_INTERNAL void m0_sns_cm_acc_cp_init(struct m0_sns_cm_cp *scp,
				       struct m0_sns_cm_ag *ag);

M0_INTERNAL int m0_sns_cm_acc_cp_setup(struct m0_sns_cm_cp *scp,
				       struct m0_fid *tgt_cobfid,
				       uint64_t tgt_cob_index,
				       uint64_t failed_unit_idx,
				       uint64_t data_seg_nr);

M0_INTERNAL struct m0_sns_cm_repair_ag *
sag2repairag(const struct m0_sns_cm_ag *sag)
{
	return container_of(sag, struct m0_sns_cm_repair_ag, rag_base);
}

M0_INTERNAL uint64_t m0_sns_cm_repair_ag_inbufs(struct m0_sns_cm *scm,
                                                const struct m0_cm_ag_id *id,
                                                struct m0_pdclust_layout *pl)
{
	uint64_t nr_cp_bufs;
	uint64_t cp_data_seg_nr;
	uint64_t nr_acc_bufs;
	uint64_t nr_in_bufs;

	cp_data_seg_nr = m0_sns_cm_data_seg_nr(scm, pl);
	/*
	 * Calculate number of buffers required for a copy packet.
	 * This depends on the unit size and the max buffer size.
	 */
	nr_cp_bufs = m0_sns_cm_cp_buf_nr(&scm->sc_ibp.sb_bp, cp_data_seg_nr);
	/* Calculate number of buffers required for incoming copy packets. */
	nr_in_bufs = m0_sns_cm_incoming_reserve_bufs(scm, id, pl);
	/* Calculate number of buffers required for accumulator copy packets. */
	nr_acc_bufs = nr_cp_bufs * m0_pdclust_K(pl);
	return  nr_in_bufs + nr_acc_bufs;
}

static int incr_recover_failure_register(struct m0_sns_cm_repair_ag *rag)
{
	struct m0_cm_cp            *cp;
	struct m0_net_buffer       *nbuf_head;
	int                         rc;
	int                         i;

	M0_PRE(rag != NULL);

	for (i = 0; i < rag->rag_base.sag_fnr; ++i) {
		cp = &rag->rag_fc[i].fc_tgt_acc_cp.sc_base;
		m0_cm_cp_bufvec_merge(cp);
		nbuf_head = cp_data_buf_tlist_head(&cp->c_buffers);
		rc = m0_sns_ir_failure_register(&nbuf_head->nb_buffer,
						rag->rag_fc[i].fc_failed_idx,
						&rag->rag_ir);
		if (rc != 0)
			goto out;
	}

out:
	return rc;
}

static int incr_recover_init(struct m0_sns_cm_repair_ag *rag,
			     struct m0_pdclust_layout *pl)
{
	uint64_t local_cp_nr;
	int      rc;

	M0_PRE(rag != NULL);
	M0_PRE(pl != NULL);

	rc = m0_parity_math_init(&rag->rag_math, m0_pdclust_N(pl),
				 m0_pdclust_K(pl));
	if (rc != 0)
		return rc;

	if (m0_pdclust_K(pl) == 1)
		return 0;

	local_cp_nr = rag->rag_base.sag_base.cag_cp_local_nr;
	rc = m0_sns_ir_init(&rag->rag_math, local_cp_nr, &rag->rag_ir);
	if (rc != 0) {
		m0_parity_math_fini(&rag->rag_math);
		return rc;
	}

	rc = incr_recover_failure_register(rag);
	if (rc != 0)
		goto err;

	rc = m0_sns_ir_mat_compute(&rag->rag_ir);
	if (rc != 0)
		goto err;

	goto out;
err:
	m0_sns_ir_fini(&rag->rag_ir);
	m0_parity_math_fini(&rag->rag_math);
out:
	return rc;
}

static void incr_recover_fini(struct m0_sns_cm_repair_ag *rag)
{
	m0_sns_ir_fini(&rag->rag_ir);
	m0_parity_math_fini(&rag->rag_math);
}

static void repair_ag_fini(struct m0_cm_aggr_group *ag)
{
	struct m0_sns_cm_ag        *sag;
	struct m0_sns_cm_repair_ag *rag;
        struct m0_cm               *cm;
        struct m0_sns_cm           *scm;
	struct m0_pdclust_layout   *pl;
	uint64_t                    total_resbufs;

	M0_ENTRY();
	M0_PRE(ag != NULL && ag->cag_layout != NULL);

	M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_sns_ag_alloc,
		     M0_ADDB_CTX_VEC(&m0_sns_ag_addb_ctx),
		     ag->cag_id.ai_hi.u_hi, ag->cag_id.ai_hi.u_lo,
		     ag->cag_id.ai_lo.u_hi, ag->cag_id.ai_lo.u_lo);


	sag = ag2snsag(ag);
	rag = sag2repairag(sag);
        cm = ag->cag_cm;
        M0_ASSERT(cm != NULL);
        scm = cm2sns(cm);
	/**
	 * Free the aggregation group if this is the last copy packet
	 * being finalised for a given aggregation group.
	 */
	if(rag->rag_acc_freed == sag->sag_fnr) {
		if (ag->cag_has_incoming) {
			pl = m0_layout_to_pdl(ag->cag_layout);
			total_resbufs = m0_sns_cm_repair_ag_inbufs(scm,
								   &ag->cag_id,
								   pl);
			m0_sns_cm_normalize_reservation(scm, ag, pl,
							total_resbufs);
		}
		incr_recover_fini(rag);
		m0_sns_cm_ag_fini(sag);
		m0_free(rag->rag_fc);
		m0_free(rag);
	}

	M0_LEAVE();
}

static bool repair_ag_can_fini(struct m0_cm_aggr_group *ag)
{
	struct m0_sns_cm_repair_ag *rag = sag2repairag(ag2snsag(ag));

	return rag->rag_acc_freed == rag->rag_base.sag_fnr;
}

static const struct m0_cm_aggr_group_ops sns_cm_repair_ag_ops = {
	.cago_ag_can_fini = repair_ag_can_fini,
	.cago_fini        = repair_ag_fini,
	.cago_local_cp_nr = m0_sns_cm_ag_local_cp_nr
};

static uint64_t repair_ag_target_unit(struct m0_sns_cm_ag *sag,
				      struct m0_pdclust_layout *pl,
				      uint64_t fdev, uint64_t funit)
{
        struct m0_poolmach *pm = sag->sag_base.cag_cm->cm_pm;
        struct m0_fid       gfid;
        uint64_t            group;
        uint32_t            tgt_unit;
        int                 rc;

        agid2fid(&sag->sag_base.cag_id, &gfid);
        group = agid2group(&sag->sag_base.cag_id);

        rc = m0_sns_repair_spare_map(pm, &gfid, pl, group, funit, &tgt_unit);
        if (rc != 0)
                tgt_unit = ~0;

        return tgt_unit;
}

static int repair_ag_failure_ctxs_setup(struct m0_sns_cm_repair_ag *rag,
					const struct m0_bitmap *fmap,
					struct m0_pdclust_layout *pl)
{
	struct m0_sns_cm    *scm = cm2sns(rag->rag_base.sag_base.cag_cm);
	struct m0_sns_cm_ag *sag = &rag->rag_base;
	struct m0_dbenv     *dbenv = scm->sc_it.si_dbenv;
	struct m0_cob_domain *cdom = scm->sc_it.si_cob_dom;
	uint64_t             tgt_unit;
	uint64_t             fidx = 0;
	int                  i;
	int                  rc = 0;
	bool                 discard_group = false;

	M0_PRE(fmap != NULL);
	M0_PRE(fmap->b_nr == (m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl)));

	for (i = 0; i < fmap->b_nr; ++i) {
		if (!m0_bitmap_get(fmap, i))
			continue;
		if (m0_sns_cm_unit_is_spare(scm, pl, i))
			continue;
		discard_group = false;
		tgt_unit = repair_ag_target_unit(sag, pl,
						 scm->sc_it.si_fdata[fidx],
						 i);
		M0_LOG(M0_FATAL, "target_unit: [%lu]", tgt_unit);
		rc = m0_sns_cm_ag_tgt_unit2cob(sag, tgt_unit, pl,
					       &rag->rag_fc[fidx].fc_tgt_cobfid);
		if (rc != 0)
			return rc;
		if (sag->sag_base.cag_has_incoming && sag->sag_base.cag_cp_local_nr == 0) {
			rc = m0_sns_cm_cob_locate(dbenv, cdom,
						  &rag->rag_fc[fidx].fc_tgt_cobfid);
			if (rc == -ENOENT) {
				M0_LOG(M0_FATAL, "id [%lu] [%lu] [%lu] [%lu] [%d]",
					sag->sag_base.cag_id.ai_hi.u_hi, sag->sag_base.cag_id.ai_hi.u_lo,
					sag->sag_base.cag_id.ai_lo.u_hi, sag->sag_base.cag_id.ai_lo.u_lo,
					sag->sag_base.cag_has_incoming);
				discard_group = true;
			} else if (rc != 0)
				return rc;
		}
		rag->rag_fc[fidx].fc_failed_idx = i;
		rag->rag_fc[fidx].fc_tgt_idx = tgt_unit;
		rag->rag_fc[fidx].fc_tgt_cob_index =
				m0_sns_cm_ag_unit2cobindex(sag, tgt_unit, pl);
		++fidx;
	}

	M0_POST(fidx == sag->sag_fnr);
	if (discard_group)
		rc = -EREMOTE;

	return rc;
}

M0_INTERNAL int m0_sns_cm_repair_ag_alloc(struct m0_cm *cm,
					  const struct m0_cm_ag_id *id,
					  bool has_incoming,
					  struct m0_cm_aggr_group **out)
{
	struct m0_sns_cm_repair_ag *rag;
	struct m0_sns_cm_ag        *sag = NULL;
	struct m0_pdclust_layout   *pl = NULL;
	uint64_t                    f_nr;
	int                         i;
	int                         rc = 0;

	M0_ENTRY("scm: %p, ag id:%p", cm, id);
	M0_PRE(cm != NULL && id != NULL && out != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	/* Allocate new aggregation group. */
	SNS_ALLOC_PTR(rag, &m0_sns_ag_addb_ctx, AG_ALLOC);
	if (rag == NULL)
		return -ENOMEM;
	rc = m0_sns_cm_ag_init(&rag->rag_base, cm, id, &sns_cm_repair_ag_ops,
			       has_incoming);
	if (rc != 0) {
		m0_free(rag);
		return rc;
	}
	f_nr = rag->rag_base.sag_fnr;
	SNS_ALLOC_ARR(rag->rag_fc, f_nr, &m0_sns_ag_addb_ctx, AG_FAIL_CTX);
	if (rag->rag_fc == NULL) {
		rc =  -ENOMEM;
		goto cleanup_ag;
	}
	sag = &rag->rag_base;
	pl = m0_layout_to_pdl(sag->sag_base.cag_layout);
	/* Set the target cob fid of accumulators for this aggregation group. */
	rc = repair_ag_failure_ctxs_setup(rag, &sag->sag_fmap, pl);
	if (rc != 0)
		goto cleanup_ag;

	/* Initialise the accumulators. */
	for (i = 0; i < sag->sag_fnr; ++i)
		m0_sns_cm_acc_cp_init(&rag->rag_fc[i].fc_tgt_acc_cp, sag);

	/* Acquire buffers and setup the accumulators. */
	rc = m0_sns_cm_repair_ag_setup(sag, pl);
	if (rc != 0 && rc != -ENOBUFS)
		goto cleanup_acc;
	*out = &sag->sag_base;

	M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_sns_ag_alloc,
		     M0_ADDB_CTX_VEC(&m0_sns_ag_addb_ctx),
		     id->ai_hi.u_hi, id->ai_hi.u_lo,
		     id->ai_lo.u_hi, id->ai_lo.u_lo);

	goto done;

cleanup_acc:
	for (i = 0; i < sag->sag_fnr; ++i)
		m0_cm_cp_buf_release(&rag->rag_fc[i].fc_tgt_acc_cp.sc_base);
cleanup_ag:
	m0_layout_put(m0_pdl_to_layout(pl));
	m0_cm_aggr_group_fini(&sag->sag_base);
	m0_bitmap_fini(&sag->sag_fmap);
	m0_free(rag->rag_fc);
	m0_free(rag);
done:
	M0_LEAVE("ag: %p", &sag->sag_base);
	return rc;
}

M0_INTERNAL int m0_sns_cm_repair_ag_setup(struct m0_sns_cm_ag *sag,
					  struct m0_pdclust_layout *pl)
{
	struct m0_sns_cm           *scm = cm2sns(sag->sag_base.cag_cm);
	struct m0_sns_cm_repair_ag *rag = sag2repairag(sag);
	struct m0_fid               gfid;
	int                         i;
	int                         rc;
	uint64_t                    cp_data_seg_nr;

	M0_PRE(sag != NULL);

	cp_data_seg_nr = m0_sns_cm_data_seg_nr(scm, pl);
	for (i = 0; i < sag->sag_fnr; ++i) {
		rc = m0_sns_cm_acc_cp_setup(&rag->rag_fc[i].fc_tgt_acc_cp,
					    &rag->rag_fc[i].fc_tgt_cobfid,
					    rag->rag_fc[i].fc_tgt_cob_index,
					    rag->rag_fc[i].fc_failed_idx,
					    cp_data_seg_nr);
		if (rc < 0)
			return rc;
	}

	agid2fid(&sag->sag_base.cag_id, &gfid);
	rc = incr_recover_init(rag, pl);

	return rc;
}

/**
 * Returns true if all the necessary copy packets are transformed or
 * accumulated into the aggregation group accumulator for a given
 * copy machine operation, viz. sns-repair or sns-rebalance.
 */
M0_INTERNAL bool m0_sns_cm_ag_acc_is_full_with(const struct m0_cm_cp *acc,
					       uint64_t nr_cps)
{
	int                         i;
	uint64_t                    xform_cp_nr = 0;

	for (i = 0; i < acc->c_xform_cp_indices.b_nr; ++i) {
		if (m0_bitmap_get(&acc->c_xform_cp_indices, i))
			M0_CNT_INC(xform_cp_nr);
	}

	M0_LOG(M0_DEBUG, "acc is full: xform_cp_nr: [%lu] cp_nr: [%lu]",
	       xform_cp_nr, nr_cps);
	return xform_cp_nr == nr_cps;
}

/** @} SNSCMAG */
#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
