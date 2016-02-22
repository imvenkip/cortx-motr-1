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
 * Original creation date: 25/08/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/trace.h"

#include "fop/fop.h"
#include "reqh/reqh.h"
#include "conf/obj_ops.h"

#include "sns/cm/cm_utils.h"
#include "sns/cm/iter.h"
#include "sns/cm/cm.h"
#include "sns/cm/cp.h"
#include "sns/cm/file.h"
#include "sns/cm/repair/ag.h"
#include "sns/cm/sw_onwire_fop.h"

extern const struct m0_sns_cm_helpers repair_helpers;
extern const struct m0_cm_cp_ops m0_sns_cm_repair_cp_ops;
extern struct m0_cm_type sns_repair_cmt;

M0_INTERNAL int
m0_sns_cm_repair_sw_onwire_fop_setup(struct m0_cm *cm, struct m0_fop *fop,
                                     void (*fop_release)(struct m0_ref *),
                                     const char *local_ep,
				     const struct m0_cm_sw *sw,
				     const struct m0_cm_ag_id *last_out);

static struct m0_cm_cp *repair_cm_cp_alloc(struct m0_cm *cm)
{
	struct m0_cm_cp *cp;

	cp = m0_sns_cm_cp_alloc(cm);
	if (cp != NULL)
		cp->c_ops = &m0_sns_cm_repair_cp_ops;

	return cp;
}

static int repair_cm_prepare(struct m0_cm *cm)
{
	struct m0_sns_cm *scm = cm2sns(cm);

	M0_ENTRY("cm: %p", cm);
	M0_PRE(scm->sc_op == SNS_REPAIR);

	scm->sc_helpers = &repair_helpers;
	return m0_sns_cm_prepare(cm);
}

static void repair_cm_stop(struct m0_cm *cm)
{
	struct m0_sns_cm       *scm = cm2sns(cm);
	struct m0_pools_common *pc = cm->cm_service.rs_reqh->rh_pools;
	struct m0_pool         *pool;
	struct m0_pooldev      *pd;
	struct m0_conf_obj     *disk_obj;
	struct m0_conf_disk    *disk;
	struct m0_conf_cache   *cc;
	struct m0_ha_nvec       nvec;
	enum m0_ha_obj_state    dstate;
	enum m0_ha_obj_state    pstate;
	int                     i = 0;
	int                     rc;

	M0_PRE(scm->sc_op == SNS_REPAIR);

	if (cm->cm_quiesce || cm->cm_abort) {
		M0_LOG(M0_DEBUG, "repair stopped by %s cmd",
				cm->cm_quiesce ? "QUIESCE" : "ABORT");
		goto out;
	}

	if (m0_cm_state_get(cm) == M0_CMS_FAIL)
		goto out;

	rc = m0_sns_cm_ha_nvec_alloc(cm, M0_PNDS_SNS_REPAIRING, &nvec);
	if (rc != 0) {
		M0_LOG(M0_ERROR, "HA note allocation failed with rc: %d", rc);
		goto out;
	}

	cc = &pc->pc_confc->cc_cache;
	m0_tl_for(pools, &pc->pc_pools, pool) {
		pstate = M0_NC_REPAIRED;
		m0_tl_for(pool_failed_devs, &pool->po_failed_devices, pd) {
			if (pd->pd_state == M0_PNDS_SNS_REPAIRING) {
				dstate = M0_NC_REPAIRED;
				rc = m0_conf_obj_find_lock(cc, &pd->pd_id,
							   &disk_obj);
				M0_ASSERT(rc == 0);
				disk = M0_CONF_CAST(disk_obj, m0_conf_disk);
				M0_ASSERT(disk != NULL);
				if (m0_sns_cm_disk_has_dirty_pver(cm, disk)) {
					dstate = M0_NC_REPAIR;
					pstate = M0_NC_REPAIR;
				}
				nvec.nv_note[i].no_id = disk_obj->co_id;
				nvec.nv_note[i].no_state = dstate;
				M0_CNT_INC(i);
			}
		} m0_tl_endfor;
		/* Set pool ha note. */
		nvec.nv_note[i].no_id = pool->po_id;
		nvec.nv_note[i].no_state = pstate;
		m0_sns_cm_ha_state_set(&nvec);
	} m0_tl_endfor;

out:

	m0_sns_cm_stop(cm);
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
static bool repair_cm_has_space(struct m0_cm *cm, const struct m0_cm_ag_id *id,
				struct m0_layout *l)
{
	struct m0_sns_cm          *scm = cm2sns(cm);
	struct m0_pdclust_layout  *pl  = m0_layout_to_pdl(l);
	struct m0_fid              fid;
	struct m0_sns_cm_file_ctx *fctx;
	uint64_t                   total_inbufs;

	M0_PRE(cm != NULL && id != NULL && pl != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	agid2fid(id, &fid);
	m0_mutex_lock(&scm->sc_file_ctx_mutex);
	fctx = m0_sns_cm_fctx_locate(scm, &fid);
	m0_mutex_unlock(&scm->sc_file_ctx_mutex);
	M0_ASSERT(fctx != NULL);
	total_inbufs = m0_sns_cm_repair_ag_inbufs(scm, fctx->sf_pm,
						  id, pl, fctx->sf_pi);
	return m0_sns_cm_has_space_for(scm, pl, total_inbufs);
}

M0_INTERNAL enum sns_repair_state
m0_sns_cm_fid_repair_done(struct m0_fid *gfid, struct m0_reqh *reqh,
			  enum m0_pool_nd_state device_state)
{
	struct m0_sns_cm       *scm;
	struct m0_cm	       *cm;
	struct m0_reqh_service *service;
	struct m0_fid           curr_gfid;

	M0_PRE(gfid != NULL && m0_fid_is_valid(gfid));
	M0_PRE(reqh != NULL);

	service = m0_reqh_service_find(&sns_repair_cmt.ct_stype, reqh);
	M0_ASSERT(service != NULL);

	cm = container_of(service, struct m0_cm, cm_service);
	scm = cm2sns(cm);

	M0_SET0(&curr_gfid);
	switch (device_state) {
	case M0_PNDS_SNS_REPAIRED:
		return SRS_REPAIR_DONE;
	case M0_PNDS_SNS_REPAIRING:
		m0_cm_lock(cm);
		curr_gfid = scm->sc_it.si_fc.ifc_gfid;
		m0_cm_unlock(cm);
		if (curr_gfid.f_container == 0 && curr_gfid.f_key == 0)
			break;
		return m0_fid_cmp(gfid, &curr_gfid) > 0 ? SRS_REPAIR_NOTDONE :
				  SRS_REPAIR_DONE;
	case M0_PNDS_SNS_REBALANCING :
		return SRS_REPAIR_NOTDONE;
	default:
		return SRS_UNINITIALIZED;
	}

	return SRS_UNINITIALIZED;
}

/** Copy machine operations. */
const struct m0_cm_ops sns_repair_ops = {
	.cmo_setup               = m0_sns_cm_setup,
	.cmo_prepare             = repair_cm_prepare,
	.cmo_start               = m0_sns_cm_start,
	.cmo_ag_alloc            = m0_sns_cm_repair_ag_alloc,
	.cmo_cp_alloc            = repair_cm_cp_alloc,
	.cmo_data_next           = m0_sns_cm_iter_next,
	.cmo_ag_next             = m0_sns_cm_ag_next,
	.cmo_has_space           = repair_cm_has_space,
	.cmo_sw_onwire_fop_setup = m0_sns_cm_repair_sw_onwire_fop_setup,
	.cmo_stop                = repair_cm_stop,
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
