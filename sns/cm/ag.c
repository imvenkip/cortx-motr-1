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
 * Original creation date: 12/09/2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM

#include "lib/trace.h"
#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/cdefs.h"
#include "lib/misc.h"

#include "fid/fid.h"

#include "sns/sns_addb.h"
#include "sns/cm/cm_utils.h"
#include "sns/cm/ag.h"
#include "sns/cm/cp.h"
#include "sns/cm/cm.h"
#include "sns/cm/iter.h"

/**
   @addtogroup SNSCMAG

   @{
 */

M0_INTERNAL void m0_sns_cm_acc_cp_init(struct m0_sns_cm_cp *scp,
				       struct m0_sns_cm_ag *ag);

M0_INTERNAL int m0_sns_cm_acc_cp_setup(struct m0_sns_cm_cp *scp,
				       struct m0_fid *tgt_cobfid,
				       uint64_t tgt_cob_index,
				       uint64_t data_seg_nr);

M0_INTERNAL struct m0_sns_cm_ag *ag2snsag(const struct m0_cm_aggr_group *ag)
{
	return container_of(ag, struct m0_sns_cm_ag, sag_base);
}

M0_INTERNAL void m0_sns_cm_ag_agid_setup(const struct m0_fid *gob_fid, uint64_t group,
                                         struct m0_cm_ag_id *agid)
{
        agid->ai_hi.u_hi = gob_fid->f_container;
        agid->ai_hi.u_lo = gob_fid->f_key;
        agid->ai_lo.u_hi = 0;
        agid->ai_lo.u_lo = group;
}

static int incr_recover_failure_register(struct m0_sns_cm_ag *sag,
					 struct m0_sns_cm *scm,
					 const struct m0_fid *fid,
					 struct m0_pdclust_layout *pl)
{
	struct m0_fid               cob_fid;
	struct m0_cm_cp            *cp;
	struct m0_net_buffer       *nbuf_head;
        struct m0_pdclust_src_addr  sa;
        struct m0_pdclust_tgt_addr  ta;
        struct m0_pdclust_instance *pi;
	uint64_t                    group;
	uint64_t                    unit;
	int                         start;
	int                         end;
	int                         rc;
	int                         i;

	M0_PRE(sag != NULL);
	M0_PRE(scm != NULL);
	M0_PRE(fid != NULL);
	M0_PRE(pl != NULL);

	rc = m0_sns_cm_fid_layout_instance(pl, &pi, fid);
	if (rc != 0)
		return rc;

	group = agid2group(&sag->sag_base.cag_id);
        sa.sa_group = group;
        start = m0_sns_cm_ag_unit_start(scm->sc_op, pl);
        end = m0_sns_cm_ag_unit_end(scm->sc_op, pl);

	for (unit = start; unit < end; ++unit) {
                M0_SET0(&ta);
                M0_SET0(&cob_fid);
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, fid, &cob_fid);
		for (i = 0; i < scm->sc_failures_nr; ++i) {
			if (cob_fid.f_container == scm->sc_it.si_fdata[i]) {
				sag->sag_fc[i].fc_failed_idx = unit;
				cp = &sag->sag_fc[i].fc_tgt_acc_cp.sc_base;
				m0_cm_cp_bufvec_merge(cp);
				nbuf_head = cp_data_buf_tlist_head(
						&cp->c_buffers);
				rc = m0_sns_ir_failure_register(
						&nbuf_head->nb_buffer,
						sag->sag_fc[i].fc_failed_idx,
						&sag->sag_ir);
				if (rc != 0)
					goto out;
			}
		}
	}
out:
	m0_layout_instance_fini(&pi->pi_base);
	return rc;
}

static int incr_recover_init(struct m0_sns_cm_ag *sag, struct m0_sns_cm *scm,
			     const struct m0_fid *fid,
			     struct m0_pdclust_layout *pl)
{
	int rc;

	M0_PRE(sag != NULL);
	M0_PRE(scm != NULL);
	M0_PRE(fid != NULL);
	M0_PRE(pl != NULL);

	rc = m0_parity_math_init(&sag->sag_math, m0_pdclust_N(pl),
				 m0_pdclust_K(pl));
	if (rc != 0)
		return rc;

	if (m0_pdclust_K(pl) == 1)
		return 0;

	rc = m0_sns_ir_init(&sag->sag_math, &sag->sag_ir);
	if (rc != 0) {
		m0_parity_math_fini(&sag->sag_math);
		return rc;
	}

	rc = incr_recover_failure_register(sag, scm, fid, pl);
	if (rc != 0)
		goto err;

	rc = m0_sns_ir_mat_compute(&sag->sag_ir);
	if (rc != 0)
		goto err;

	goto out;
err:
	m0_sns_ir_fini(&sag->sag_ir);
	m0_parity_math_fini(&sag->sag_math);
out:
	return rc;
}

static void incr_recover_fini(struct m0_sns_cm_ag *sag)
{
	m0_sns_ir_fini(&sag->sag_ir);
	m0_parity_math_fini(&sag->sag_math);
}

static void ag_fini(struct m0_cm_aggr_group *ag)
{
	struct m0_sns_cm_ag *sag;
        struct m0_cm        *cm;
        struct m0_sns_cm    *scm;

	M0_ENTRY();
	M0_PRE(ag != NULL && ag->cag_layout != NULL);

	M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_sns_ag_alloc,
		     M0_ADDB_CTX_VEC(&m0_sns_ag_addb_ctx),
		     ag->cag_id.ai_hi.u_hi, ag->cag_id.ai_hi.u_lo,
		     ag->cag_id.ai_lo.u_hi, ag->cag_id.ai_lo.u_lo);


	sag = ag2snsag(ag);
	M0_CNT_INC(sag->sag_acc_freed);
	/**
	 * Free the aggregation group if this is the last copy packet
	 * being finalised for a given aggregation group.
	 */
	if(sag->sag_acc_freed == sag->sag_fnr) {
		if (ag->cag_has_incoming)
			m0_sns_cm_normalize_reservation(ag->cag_cm, ag);
		incr_recover_fini(sag);
		m0_cm_aggr_group_fini_and_progress(ag);
		if (ag->cag_layout != NULL) {
			m0_layout_put(ag->cag_layout);
			ag->cag_layout = NULL;
		}

		m0_free(sag->sag_fc);
		m0_free(sag);
	}
        cm = ag->cag_cm;
        M0_ASSERT(cm != NULL);
        scm = cm2sns(cm);
	M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_sns_cm_buf_nr,
		     M0_ADDB_CTX_VEC(&m0_sns_ag_addb_ctx),
		     scm->sc_ibp.sb_bp.nbp_buf_nr,
		     scm->sc_obp.sb_bp.nbp_buf_nr,
		     scm->sc_ibp.sb_bp.nbp_free,
		     scm->sc_obp.sb_bp.nbp_free);

	M0_LEAVE();
}

M0_INTERNAL void agid2fid(const struct m0_cm_ag_id *id, struct m0_fid *fid)
{
	M0_PRE(id != NULL);
	M0_PRE(fid != NULL);

        m0_fid_set(fid, id->ai_hi.u_hi, id->ai_hi.u_lo);
}

M0_INTERNAL uint64_t agid2group(const struct m0_cm_ag_id *id)
{
	M0_PRE(id != NULL);

	return id->ai_lo.u_lo;
}

static uint64_t ag_local_cp_nr(const struct m0_cm_aggr_group *ag)
{
	struct m0_fid             fid;
	struct m0_cm             *cm;
	struct m0_sns_cm         *scm;
	struct m0_pdclust_layout *pl;
	uint64_t                  group;

	M0_ENTRY();
	M0_PRE(ag != NULL && ag->cag_layout != NULL);

	agid2fid(&ag->cag_id, &fid);
	group = agid2group(&ag->cag_id);

	cm = ag->cag_cm;
	M0_ASSERT(cm != NULL);
	scm = cm2sns(cm);

	pl = m0_layout_to_pdl(ag->cag_layout);

	M0_LEAVE();
	return m0_sns_cm_ag_nr_local_units(scm, &fid, pl, group);
}

static bool ag_can_fini(struct m0_cm_aggr_group *ag, struct m0_cm_cp *cp)
{
	struct m0_sns_cm_cp *scp = cp2snscp(cp);
        uint64_t             nr_cps;

	M0_PRE(ag != NULL && cp != NULL);

        if (ag->cag_has_incoming) {
		/* Check if this is local accumulator. */
		if (!scp->sc_is_acc)
			return false;
		nr_cps = m0_cm_cp_nr(cp);
		return m0_sns_cm_ag_relevant_is_done(ag, nr_cps);
        } else
		return m0_sns_cm_ag_local_is_done(ag);
}

static const struct m0_cm_aggr_group_ops sns_cm_ag_ops = {
	.cago_ag_can_fini = ag_can_fini,
	.cago_fini        = ag_fini,
	.cago_local_cp_nr = ag_local_cp_nr
};

M0_INTERNAL int m0_sns_cm_ag_alloc(struct m0_cm *cm,
				   const struct m0_cm_ag_id *id,
				   bool has_incoming,
				   struct m0_cm_aggr_group **out)
{
	struct m0_sns_cm           *scm = cm2sns(cm);
	struct m0_sns_cm_ag        *sag;
	struct m0_fid               gfid;
	struct m0_pdclust_layout   *pl;
	struct m0_pdclust_instance *pi;
	uint64_t                    fsize;
	uint64_t                    f_nr;
	struct m0_fid              *it_gfid;
	int                         i;
	int                         rc = 0;

	M0_ENTRY("scm: %p, ag id:%p", cm, id);
	M0_PRE(cm != NULL && id != NULL && out != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	agid2fid(id, &gfid);
	/*
	 * If m0_cm_aggr_group_alloc() is invoked in sns data iterator context,
	 * the file size and layout are already fetched and saved in the data
	 * iterator. So check if the iterator file identifier and group's file
	 * identifier match, if yes, then extract the layout from the iterator.
	 * @todo Interface to fetch the file size and layout from the sns data
	 *       iterator.
	 */
	it_gfid = &scm->sc_it.si_fc.sfc_gob_fid;
	if (m0_fid_eq(&gfid, it_gfid) && scm->sc_it.si_fc.sfc_pdlayout != NULL){
		pl = scm->sc_it.si_fc.sfc_pdlayout;
		m0_layout_get(m0_pdl_to_layout(pl));
	} else
		rc = m0_sns_cm_file_size_layout_fetch(&scm->sc_base, &gfid, &pl,
						      &fsize);
	if (rc != 0)
		return rc;
	rc = m0_sns_cm_fid_layout_instance(pl, &pi, &gfid);
	if (rc != 0) {
		m0_layout_put(m0_pdl_to_layout(pl));
		return rc;
	}
	/* calculate actual failed number of units in this group. */
	f_nr = m0_sns_cm_ag_failures_nr(scm, &gfid, pl, pi, id->ai_lo.u_lo);
	m0_layout_instance_fini(&pi->pi_base);
	M0_ASSERT(f_nr != 0);
	if (f_nr == 0)
		return -EINVAL;
	/* Allocate new aggregation group. */
	SNS_ALLOC_PTR(sag, &m0_sns_ag_addb_ctx, AG_ALLOC);
	if (sag == NULL) {
		m0_layout_put(m0_pdl_to_layout(pl));
		return -ENOMEM;
	}
	SNS_ALLOC_ARR(sag->sag_fc, f_nr, &m0_sns_ag_addb_ctx, AG_FAIL_CTX);
	if (sag->sag_fc == NULL) {
		m0_layout_put(m0_pdl_to_layout(pl));
		m0_free(sag);
		return -ENOMEM;
	}
	sag->sag_fnr = f_nr;
	sag->sag_base.cag_layout = m0_pdl_to_layout(pl);
	m0_cm_aggr_group_init(&sag->sag_base, cm, id, has_incoming,
			      &sns_cm_ag_ops);
	sag->sag_base.cag_cp_global_nr = m0_sns_cm_ag_nr_global_units(scm, pl);
	/* Set the target cob fid of accumulators for this aggregation group. */
	rc = m0_sns_cm_ag_tgt_unit2cob(sag, pl);
	if (rc != 0)
		goto cleanup_ag;

	/* Initialise the accumulators. */
	for (i = 0; i < sag->sag_fnr; ++i) {
		m0_sns_cm_acc_cp_init(&sag->sag_fc[i].fc_tgt_acc_cp, sag);
		sag->sag_fc[i].fc_failed_idx = ~0;
	}

	/* Acquire buffers and setup the accumulators. */
	rc = m0_sns_cm_ag_setup(sag, pl);
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
		m0_cm_cp_buf_release(&sag->sag_fc[i].fc_tgt_acc_cp.sc_base);
cleanup_ag:
	m0_layout_put(m0_pdl_to_layout(pl));
	m0_cm_aggr_group_fini(&sag->sag_base);
	m0_free(sag->sag_fc);
	m0_free(sag);
done:
	M0_LEAVE("ag: %p", sag);
	return rc;
}

M0_INTERNAL int m0_sns_cm_ag_setup(struct m0_sns_cm_ag *sag,
				   struct m0_pdclust_layout *pl)
{
	struct m0_sns_cm *scm = cm2sns(sag->sag_base.cag_cm);
	struct m0_fid     gfid;
	int               i;
	int               rc;
	uint64_t          cp_data_seg_nr;

	M0_PRE(sag != NULL);

	cp_data_seg_nr = m0_sns_cm_data_seg_nr(scm, pl);
	for (i = 0; i < sag->sag_fnr; ++i) {
		rc = m0_sns_cm_acc_cp_setup(&sag->sag_fc[i].fc_tgt_acc_cp,
					    &sag->sag_fc[i].fc_tgt_cobfid,
					    sag->sag_fc[i].fc_tgt_cob_index,
					    cp_data_seg_nr);
		if (rc < 0)
			return rc;
	}

	agid2fid(&sag->sag_base.cag_id, &gfid);
	rc = incr_recover_init(sag, scm, &gfid, pl);
	return rc;
}

#undef M0_TRACE_SUBSYSTEM

/** @} SNSCMAG */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
