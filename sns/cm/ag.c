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
#include "sns/cm/ag.h"
#include "sns/cm/cp.h"
#include "sns/cm/cm.h"
#include "sns/cm/iter.h"

/**
   @addtogroup SNSCMAG

   @{
 */

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

M0_INTERNAL uint64_t m0_sns_cm_ag_local_cp_nr(const struct m0_cm_aggr_group *ag)
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

M0_INTERNAL void m0_sns_cm_ag_fini(struct m0_sns_cm_ag *sag)
{
        struct m0_cm_aggr_group *ag;
        struct m0_cm            *cm;
        struct m0_sns_cm        *scm;

        M0_ENTRY();
        M0_PRE(sag != NULL);

	ag = &sag->sag_base;
        M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_sns_ag_alloc,
                     M0_ADDB_CTX_VEC(&m0_sns_ag_addb_ctx),
                     ag->cag_id.ai_hi.u_hi, ag->cag_id.ai_hi.u_lo,
                     ag->cag_id.ai_lo.u_hi, ag->cag_id.ai_lo.u_lo);
        cm = ag->cag_cm;
        M0_ASSERT(cm != NULL);
	m0_cm_aggr_group_fini_and_progress(ag);
	if (ag->cag_layout != NULL) {
		m0_layout_put(ag->cag_layout);
		ag->cag_layout = NULL;
	}
	m0_bitmap_fini(&sag->sag_fmap);
        scm = cm2sns(cm);
        M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_sns_cm_buf_nr,
                     M0_ADDB_CTX_VEC(&m0_sns_ag_addb_ctx),
                     scm->sc_ibp.sb_bp.nbp_buf_nr,
                     scm->sc_obp.sb_bp.nbp_buf_nr,
                     scm->sc_ibp.sb_bp.nbp_free,
                     scm->sc_obp.sb_bp.nbp_free);

        M0_LEAVE();
}

M0_INTERNAL int m0_sns_cm_ag_init(struct m0_sns_cm_ag *sag,
				  struct m0_cm *cm,
				  const struct m0_cm_ag_id *id,
				  const struct m0_cm_aggr_group_ops *ag_ops,
				  bool has_incoming)
{
	struct m0_sns_cm           *scm = cm2sns(cm);
	struct m0_fid               gfid;
	struct m0_pdclust_layout   *pl;
	struct m0_pdclust_instance *pi;
	uint64_t                    upg;
	uint64_t                    fsize;
	uint64_t                    f_nr;
	struct m0_fid              *it_gfid;
	int                         rc = 0;

	M0_ENTRY("scm: %p, ag id:%p", cm, id);
	M0_PRE(sag != NULL && cm != NULL && id != NULL && ag_ops != NULL);
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

	upg = m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
	m0_bitmap_init(&sag->sag_fmap, upg);
	/* calculate actual failed number of units in this group. */
	f_nr = m0_sns_cm_ag_failures_nr(scm, &gfid, pl, pi, id->ai_lo.u_lo,
					&sag->sag_fmap);
	m0_layout_instance_fini(&pi->pi_base);
	if (f_nr == 0) {
		m0_layout_put(m0_pdl_to_layout(pl));
		return -EINVAL;
	}
	sag->sag_fnr = f_nr;
	sag->sag_base.cag_layout = m0_pdl_to_layout(pl);
	m0_cm_aggr_group_init(&sag->sag_base, cm, id, has_incoming,
			      ag_ops);
	sag->sag_base.cag_cp_global_nr = m0_sns_cm_ag_nr_global_units(scm, pl);
	M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_sns_ag_alloc,
		     M0_ADDB_CTX_VEC(&m0_sns_ag_addb_ctx),
		     id->ai_hi.u_hi, id->ai_hi.u_lo,
		     id->ai_lo.u_hi, id->ai_lo.u_lo);

	M0_LEAVE("ag: %p", sag);
	return rc;
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
