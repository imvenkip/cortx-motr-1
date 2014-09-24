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
#include "sns/cm/file.h"

/**
   @addtogroup SNSCMAG

   @{
 */

M0_INTERNAL struct m0_cm *snsag2cm(const struct m0_sns_cm_ag *sag)
{
	return sag->sag_base.cag_cm;
}

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
	scm = cm2sns(cm);
	m0_layout_instance_fini(&sag->sag_base.cag_pi->pi_base);
	m0_bitmap_fini(&sag->sag_fmap);
	m0_sns_cm_fctx_put(scm, &ag->cag_id);
	m0_cm_aggr_group_fini_and_progress(ag);
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
	struct m0_sns_cm_file_ctx  *fctx;
	struct m0_pdclust_layout   *pl;
	struct m0_pdclust_instance *pi;
	uint64_t                    upg;
	uint64_t                    f_nr;
	int                         rc = 0;

	M0_ENTRY("scm: %p, ag id:%p", cm, id);
	M0_PRE(sag != NULL && cm != NULL && id != NULL && ag_ops != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	agid2fid(id, &gfid);
	fctx = m0_sns_cm_fctx_get(scm, id);
	M0_ASSERT(fctx != NULL && fctx->sf_layout != NULL);
	pl = m0_layout_to_pdl(fctx->sf_layout);
	rc = m0_sns_cm_fid_layout_instance(pl, &pi, &gfid);
	if (rc != 0) {
		m0_sns_cm_fctx_put(scm, id);
		return rc;
	}

	upg = m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
	m0_bitmap_init(&sag->sag_fmap, upg);
	/* calculate actual failed number of units in this group. */
	f_nr = m0_sns_cm_ag_failures_nr(scm, &gfid, pl, pi, id->ai_lo.u_lo,
					&sag->sag_fmap);
	if (f_nr == 0) {
		m0_layout_instance_fini(&pi->pi_base);
		m0_bitmap_fini(&sag->sag_fmap);
		m0_sns_cm_fctx_put(scm, id);
		return -EINVAL;
	}
	sag->sag_fnr = f_nr;
	if (has_incoming)
		sag->sag_incoming_nr = m0_sns_cm_ag_max_incoming_units(scm,
								       id, pl);
	sag->sag_base.cag_layout = fctx->sf_layout;
	sag->sag_base.cag_pi = pi;
	m0_cm_aggr_group_init(&sag->sag_base, cm, id, has_incoming,
			      ag_ops);
	sag->sag_base.cag_cp_global_nr = m0_sns_cm_ag_nr_global_units(sag, pl);
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
