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
 * Original creation date: 07/03/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM

#include "lib/trace.h"
#include "lib/misc.h"

#include "fid/fid.h"

#include "sns/parity_repair.h"
#include "sns/cm/cm_utils.h"
#include "sns/cm/ag.h"
#include "sns/cm/file.h"
#include "pool/pool.h"
#include "ioservice/io_device.h"

M0_INTERNAL int m0_sns_cm_rebalance_ag_setup(struct m0_sns_cm_ag *sag,
					     struct m0_pdclust_layout *pl);

static uint64_t
rebalance_ag_max_incoming_units(const struct m0_sns_cm *scm,
				const struct m0_cm_ag_id *id,
				struct m0_pdclust_layout *pl,
				struct m0_pdclust_instance *pi,
				struct m0_bitmap *proxy_in_map)
{
        struct m0_poolmach         *pm = scm->sc_base.cm_pm;
        struct m0_fid               cobfid;
        struct m0_fid               gfid;
        struct m0_pdclust_src_addr  sa;
        struct m0_pdclust_tgt_addr  ta;
        int32_t                     incoming_nr = 0;
        uint32_t                    tgt_unit;
        uint32_t                    tgt_unit_prev;
        uint64_t                    unit;
	uint64_t                    dpupg;
        int                         rc;

        M0_SET0(&sa);
        agid2fid(id, &gfid);
        sa.sa_group = agid2group(id);
	dpupg = m0_pdclust_N(pl) + m0_pdclust_K(pl);
	for (unit = 0; unit < dpupg; ++unit) {
		sa.sa_unit = unit;
		M0_SET0(&ta);
		M0_SET0(&cobfid);
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, &gfid, &cobfid);
		if (!m0_sns_cm_is_cob_failed(scm, &cobfid))
			continue;
                if (!m0_sns_cm_is_local_cob(&scm->sc_base, &cobfid))
                        continue;
                rc = m0_sns_repair_spare_map(pm, &gfid, pl, pi, sa.sa_group,
					     unit, &tgt_unit, &tgt_unit_prev);
                if (rc != 0)
                        return ~0;
		M0_SET0(&ta);
		M0_SET0(&cobfid);
                sa.sa_unit = tgt_unit;
                m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, &gfid, &cobfid);
                if (!m0_sns_cm_is_local_cob(&scm->sc_base, &cobfid))
			M0_CNT_INC(incoming_nr);
	}
        return incoming_nr;
}

static uint64_t rebalance_ag_unit_start(const struct m0_pdclust_layout *pl)
{
	return m0_sns_cm_ag_spare_unit_nr(pl, 0);
}

static uint64_t rebalance_ag_unit_end(const struct m0_pdclust_layout *pl)
{
	return m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
}

M0_INTERNAL int m0_sns_cm_rebalance_tgt_info(struct m0_sns_cm_ag *sag,
					     struct m0_sns_cm_cp *scp)
{
	struct m0_pdclust_layout  *pl;
	struct m0_fid              cobfid;
	struct m0_sns_cm_file_ctx *fctx;
	uint64_t                   offset;
	int                        rc = 0;

	fctx = sag->sag_fctx;
	pl = m0_layout_to_pdl(fctx->sf_layout);
	rc = m0_sns_cm_ag_tgt_unit2cob(sag, scp->sc_base.c_ag_cp_idx,
				       pl, fctx->sf_pi, &cobfid);
	if (rc == 0) {
		offset = m0_sns_cm_ag_unit2cobindex(sag,
						    scp->sc_base.c_ag_cp_idx,
						    pl, fctx->sf_pi);
		/*
		 * Change the target cobfid and offset of the copy
		 * packet to write the data from spare unit back to
		 * previously failed data unit but now available data/
		 * parity unit in the aggregation group.
		 */
		m0_sns_cm_cp_tgt_info_fill(scp, &cobfid, offset,
					   scp->sc_base.c_ag_cp_idx);
	}

	return M0_RC(rc);
}

static bool rebalance_ag_is_relevant(struct m0_sns_cm *scm,
				     const struct m0_fid *gfid,
				     uint64_t group,
				     struct m0_pdclust_layout *pl,
				     struct m0_pdclust_instance *pi)
{
	struct m0_poolmach         *pm = scm->sc_base.cm_pm;
	uint32_t                    spare;
	uint32_t                    spare_prev;
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_fid               cobfid;
	uint32_t                    N;
	uint32_t                    K;
	uint32_t                    i;
	uint32_t                    funit;
	bool                        result = false;
	int                         rc;

	N = m0_pdclust_N(pl);
	K = m0_pdclust_K(pl);
	sa.sa_group = group;
	for (i = 0; i < N + K; ++i) {
		sa.sa_unit = i;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, gfid, &cobfid);
		if (m0_sns_cm_is_cob_failed(scm, &cobfid) &&
		    m0_sns_cm_is_local_cob(&scm->sc_base, &cobfid)) {
			do {
				funit = sa.sa_unit;
				rc = m0_sns_repair_spare_map(pm, gfid,
						pl, pi, group, funit,
						&spare, &spare_prev);
				if (rc != 0)
					return false;
				sa.sa_unit = spare;
				m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta,
						      gfid, &cobfid);
			} while (m0_sns_cm_is_cob_failed(scm, &cobfid));

			if (!m0_sns_cm_is_local_cob(&scm->sc_base, &cobfid))
				result = true;
		}
	}

	return result;
}

/**
 * Reopen the REPAIRED stob devices.
 * When the disk is powered OFF and ON the disk device file (under /dev) gets
 * changed. Destroy & create the stob to reopen the underlying devices.
 */
M0_INTERNAL int m0_sns_reopen_stob_devices(struct m0_cm *cm)
{
	struct m0_poolmach    *pm;
	struct m0_sns_cm      *scm = cm2sns(cm);
	enum m0_pool_nd_state  state;
	int                    rc;

	state = scm->sc_op == SNS_REPAIR ? M0_PNDS_SNS_REPAIRING :
		M0_PNDS_SNS_REBALANCING;
	M0_PRE(state == M0_PNDS_SNS_REBALANCING);
	pm = m0_ios_poolmach_get(scm->sc_base.cm_service.rs_reqh);
	rc = m0_pool_device_reopen(pm, scm->sc_base.cm_service.rs_reqh);
	if (rc != 0) {
		return M0_ERR(rc);
	}
	return M0_RC(rc);
}

int rebalance_cob_locate(struct m0_sns_cm *scm, struct m0_cob_domain *cdom,
                        const struct m0_fid *cob_fid)
{
	return m0_sns_cm_cob_locate(cdom, cob_fid);
}

const struct m0_sns_cm_helpers rebalance_helpers = {
	.sch_ag_max_incoming_units  = rebalance_ag_max_incoming_units,
	.sch_ag_unit_start          = rebalance_ag_unit_start,
	.sch_ag_unit_end            = rebalance_ag_unit_end,
	.sch_ag_is_relevant         = rebalance_ag_is_relevant,
	.sch_ag_setup               = m0_sns_cm_rebalance_ag_setup,
	.sch_cob_locate             = rebalance_cob_locate
};

#undef M0_TRACE_SUBSYSTEM

/** @} endgroup SNSCM */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
