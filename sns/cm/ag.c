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

#include "fid/fid.h"

#include "sns/cm/cm_utils.h"
#include "sns/cm/ag.h"
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

static void ag_fini(struct m0_cm_aggr_group *ag)
{
	struct m0_sns_cm_ag *sag;

	M0_ENTRY();
	M0_PRE(ag != NULL && ag->cag_layout != NULL);

	sag = ag2snsag(ag);
	M0_CNT_INC(sag->sag_acc_freed);
	/**
	 * Free the aggregation group if this is the last copy packet
	 * being finalised for a given aggregation group.
	 */
	if(sag->sag_acc_freed == sag->sag_fnr) {
		m0_cm_aggr_group_fini_and_progress(ag);
		m0_free(sag->sag_fc);
		m0_free(sag);
	}
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
	struct m0_sns_cm_ag *sag = ag2snsag(ag);
        uint64_t             nr_cps;

	M0_PRE(ag != NULL && cp != NULL);

        if (ag->cag_has_incoming) {
                nr_cps = m0_cm_cp_nr(cp);
                return nr_cps == ag->cag_cp_global_nr - sag->sag_fnr;
        } else
		return ag->cag_freed_cp_nr == ag->cag_cp_local_nr + sag->sag_fnr;
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
	struct m0_sns_cm         *scm = cm2sns(cm);
	struct m0_sns_cm_ag      *sag;
	struct m0_fid             gfid;
	struct m0_pdclust_layout *pl;
	uint64_t                  fsize;
	uint64_t                  f_nr;
	struct m0_fid            *it_gfid;
	int                       i;
	int                       rc = 0;

	M0_ENTRY("scm: %p, ag id:%p", cm, id);
	M0_PRE(cm != NULL && id != NULL && out != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	agid2fid(id, &gfid);
	it_gfid = &scm->sc_it.si_fc.sfc_gob_fid;
	if (m0_fid_eq(&gfid, it_gfid) && scm->sc_it.si_fc.sfc_pdlayout != NULL)
		pl = scm->sc_it.si_fc.sfc_pdlayout;
	else
		rc = m0_sns_cm_file_size_layout_fetch(&scm->sc_base, &gfid, &pl,
						      &fsize);
	if (rc != 0)
		return rc;
	/*
	 * Allocate new aggregation group and add it to the
	 * lexicographically sorted list of aggregation groups in the
	 * sliding window.
	 */
	M0_ALLOC_PTR(sag);
	if (sag == NULL) {
		m0_layout_put(m0_pdl_to_layout(pl));
		return -ENOMEM;
	}
	f_nr = m0_pdclust_K(pl);
	M0_ASSERT(f_nr != 0);
	M0_ALLOC_ARR(sag->sag_fc, f_nr);
	if (sag->sag_fc == NULL) {
		m0_layout_put(m0_pdl_to_layout(pl));
		m0_free(sag);
		return -ENOMEM;
	}
	sag->sag_fnr = f_nr;
	sag->sag_base.cag_layout = m0_pdl_to_layout(pl);
	m0_cm_aggr_group_init(&sag->sag_base, cm, id, has_incoming,
			      &sns_cm_ag_ops);
	sag->sag_base.cag_cp_global_nr = m0_pdclust_N(pl) + m0_pdclust_K(pl);
	rc = m0_sns_cm_ag_tgt_unit2cob(sag, pl);
	if (rc != 0)
		goto cleanup_ag;
	for (i = 0; i < sag->sag_fnr; ++i)
		m0_sns_cm_acc_cp_init(&sag->sag_fc[i].fc_tgt_acc_cp, sag);
	rc = m0_sns_cm_ag_setup(sag, pl);
	if (rc != 0 && rc != -ENOBUFS)
		goto cleanup_acc;
	*out = &sag->sag_base;
	goto done;

cleanup_acc:
	for (i = 0; i < sag->sag_fnr; ++i)
		m0_cm_cp_buf_release(&sag->sag_fc[i].fc_tgt_acc_cp.sc_base);
cleanup_ag:
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
	struct m0_sns_cm    *scm = cm2sns(sag->sag_base.cag_cm);
	int                  i;
	int                  rc;
	uint64_t             cp_data_seg_nr;

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

	return 0;
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
