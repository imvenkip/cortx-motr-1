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
 * Original creation date: 11/06/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM
#include "lib/trace.h"

#include "lib/bob.h"
#include "lib/misc.h"  /* M0_BITS */
#include "lib/errno.h" /* ENOENT EPERM */

#include "reqh/reqh.h"
#include "sm/sm.h"
#include "rpc/rpc_opcodes.h" /* M0_CM_SW_UPDATE_OPCODE */

#include "cm/sw.h"
#include "cm/cm.h"

#include "be/op.h"           /* M0_BE_OP_SYNC */

/**
   @defgroup CMSWFOM sliding window update fom
   @ingroup CMSW

   Implementation of sliding window update FOM.
   Provides mechanism to handle blocking operations like local sliding
   update and updating the persistent store with new sliding window.
   Provides interfaces to start, wakeup (if idle) and stop the sliding
   window update FOM.

   @{
*/

enum cm_sw_update_fom_phase {
	SWU_STORE_INIT       = M0_FOM_PHASE_INIT,
	SWU_FINI	     = M0_FOM_PHASE_FINISH,
	SWU_STORE_INIT_WAIT,
	SWU_UPDATE,
	SWU_STORE,
	SWU_STORE_WAIT,
	SWU_COMPLETE,
	SWU_NR
};

static const struct m0_fom_type_ops cm_sw_update_fom_type_ops = {
};

static struct m0_sm_state_descr cm_sw_update_sd[SWU_NR] = {
	[SWU_STORE_INIT] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "Store init",
		.sd_allowed = M0_BITS(SWU_STORE_INIT_WAIT, SWU_UPDATE, SWU_FINI)
	},
	[SWU_STORE_INIT_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "Store init wait",
		.sd_allowed = M0_BITS(SWU_STORE_INIT_WAIT, SWU_UPDATE, SWU_FINI)
	},


	[SWU_UPDATE] = {
		.sd_flags   = 0,
		.sd_name    = "Update",
		.sd_allowed = M0_BITS(SWU_STORE, SWU_UPDATE, SWU_COMPLETE,
				      SWU_FINI)
	},

	[SWU_STORE] = {
		.sd_flags   = 0,
		.sd_name    = "Store",
		.sd_allowed = M0_BITS(SWU_STORE_WAIT, SWU_FINI)
	},
	[SWU_STORE_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "Update wait",
		.sd_allowed = M0_BITS(SWU_UPDATE, SWU_FINI)
	},
	[SWU_COMPLETE] = {
		.sd_flags   = 0,
		.sd_name    = "Update complete",
		.sd_allowed = M0_BITS(SWU_STORE_WAIT, SWU_FINI)
	},
	[SWU_FINI] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "Fini",
		.sd_allowed = 0
	},
};

struct m0_sm_conf cm_sw_update_conf = {
	.scf_name      = "sm: sw update conf",
	.scf_nr_states = ARRAY_SIZE(cm_sw_update_sd),
	.scf_state     = cm_sw_update_sd,
};

static struct m0_cm *cm_swu2cm(struct m0_cm_sw_update *swu)
{
	return container_of(swu, struct m0_cm, cm_sw_update);
}

static struct m0_cm_sw_update *cm_fom2swu(struct m0_fom *fom)
{
	return container_of(fom, struct m0_cm_sw_update, swu_fom);
}

static int swu_store_init(struct m0_cm_sw_update *swu)
{
	struct m0_cm  *cm = cm_swu2cm(swu);
	struct m0_fom *fom = &swu->swu_fom;
	int            phase;
	int            rc;

	rc = m0_cm_sw_store_init(cm, &fom->fo_loc->fl_group,
				 &fom->fo_tx.tx_betx);
	if (rc == 0 || rc == -ENOENT) {
		/*
		 * If existing sliding window found, we are ready to do update.
		 * If not, a new persistent sw is allocated and we need to wait.
		 */
		phase = rc == -ENOENT ? SWU_STORE_INIT_WAIT : SWU_UPDATE;
		m0_fom_phase_set(fom, phase);
		rc = M0_FSO_AGAIN;
	}

	return M0_RC(rc);
}

static int swu_store_init_wait(struct m0_cm_sw_update *swu)
{
	struct m0_cm    *cm = cm_swu2cm(swu);
	struct m0_fom   *fom = &swu->swu_fom;
	struct m0_be_tx *tx = &fom->fo_tx.tx_betx;
	struct m0_cm_sw *sw;
	int              rc;

	switch (m0_be_tx_state(tx)) {
	case M0_BTS_FAILED :
		return tx->t_sm.sm_rc;
	case M0_BTS_GROUPING :
	case M0_BTS_OPENING :
		break;
	case M0_BTS_ACTIVE :
		rc = m0_cm_sw_store_alloc(cm, tx);
		if (rc != 0)
			return M0_RC(rc);
		break;
	case M0_BTS_DONE :
		rc = tx->t_sm.sm_rc;
		m0_be_tx_fini(tx);
		if (rc != 0)
			return M0_RC(rc);
		rc = m0_cm_sw_store_load(cm, &sw);
		if (rc != 0)
			return M0_RC(rc);
		cm->cm_sw_last_updated_hi = sw->sw_lo;
		m0_fom_phase_move(fom, 0, SWU_UPDATE);
		return M0_FSO_AGAIN;
	default :
		break;
	}
	m0_fom_wait_on(fom, &tx->t_sm.sm_chan,
			&fom->fo_cb);

	return M0_FSO_WAIT;
}

static int swu_update(struct m0_cm_sw_update *swu)
{
	struct m0_cm            *cm = cm_swu2cm(swu);
	struct m0_fom           *fom = &swu->swu_fom;
	struct m0_cm_aggr_group *hi;
	struct m0_cm_aggr_group *lo;
	int                      rc;

	m0_cm_lock(cm);
	rc = m0_cm_sw_local_update(cm);
	if (rc == M0_FSO_WAIT)
		goto out;
	if (M0_IN(rc, (0, -ENOBUFS, -ENOENT, -ENODATA))) {
		M0_SET0(&swu->swu_sw);
		hi = m0_cm_ag_hi(cm);
		lo = m0_cm_ag_lo(cm);
		if (hi == NULL || lo == NULL) {
			swu->swu_is_complete = true;
			rc = M0_FSO_AGAIN;
		} else {
			m0_cm_sw_set(&swu->swu_sw, &lo->cag_id,
				     &hi->cag_id);
		}
		rc = rc == -ENOBUFS ? M0_FSO_WAIT : M0_FSO_AGAIN;
	}

	if (rc < 0) {
		swu->swu_is_complete = true;
		M0_LOG(M0_DEBUG, "SWU COMPLETE!! rc: %d", rc);
		m0_fom_phase_move(fom, 0, SWU_COMPLETE);
		rc = M0_FSO_AGAIN;
	} else
		m0_fom_phase_move(fom, 0, SWU_STORE);
out:
	m0_cm_ready_done(cm);
	m0_cm_unlock(cm);
	return M0_RC(rc);
}

static int swu_store(struct m0_cm_sw_update *swu)
{
	struct m0_cm             *cm = cm_swu2cm(swu);
	struct m0_fom            *fom = &swu->swu_fom;
	struct m0_dtx            *tx = &fom->fo_tx;
	struct m0_be_seg         *seg  = cm->cm_service.rs_reqh->rh_beseg;
	int                       rc;

	if (tx->tx_state < M0_DTX_INIT) {
		m0_dtx_init(tx, seg->bs_domain,
				&fom->fo_loc->fl_group);
		tx->tx_betx_cred = M0_BE_TX_CREDIT_TYPE(struct m0_cm_sw);
	}

	if (m0_be_tx_state(&tx->tx_betx) == M0_BTS_PREPARE) {
		m0_dtx_open(tx);
		return M0_FSO_AGAIN;
	}
        else if (M0_IN(m0_be_tx_state(&tx->tx_betx), (M0_BTS_OPENING,
						      M0_BTS_GROUPING))) {
		m0_fom_wait_on(fom, &tx->tx_betx.t_sm.sm_chan, &fom->fo_cb);
		return M0_FSO_WAIT;
	}

	m0_dtx_opened(tx);
	rc = m0_cm_sw_store_update(cm, &tx->tx_betx, &swu->swu_sw);
	if (rc != 0)
		return M0_RC(rc);
	m0_fom_wait_on(fom, &tx->tx_betx.t_sm.sm_chan, &fom->fo_cb);
	m0_dtx_done(tx);
	m0_fom_phase_move(fom, rc, SWU_STORE_WAIT);

	return M0_FSO_WAIT;
}

static int swu_store_wait(struct m0_cm_sw_update *swu)
{
	struct m0_cm   *cm = cm_swu2cm(swu);
	struct m0_fom  *fom = &swu->swu_fom;
	struct m0_dtx  *tx = &fom->fo_tx;
	int             rc = M0_FSO_WAIT;

	if (tx->tx_state == M0_DTX_DONE) {
		if (m0_be_tx_state(&tx->tx_betx) != M0_BTS_DONE) {
			m0_fom_wait_on(fom, &tx->tx_betx.t_sm.sm_chan,
				       &fom->fo_cb);
		} else {
			m0_dtx_fini(tx);
			cm->cm_sw_last_persisted = swu->swu_sw;
			M0_SET0(tx);
			if (swu->swu_is_complete) {
				m0_fom_phase_move(fom, 0, SWU_FINI);
			} else {
				m0_fom_phase_move(fom, 0, SWU_UPDATE);
				rc = M0_FSO_AGAIN;
			}
		}
	}

	return rc;
}

static int swu_complete(struct m0_cm_sw_update *swu)
{
	struct m0_cm     *cm = cm_swu2cm(swu);
	struct m0_fom    *fom = &swu->swu_fom;
	struct m0_dtx    *tx = &fom->fo_tx;
	struct m0_be_seg *seg = cm->cm_service.rs_reqh->rh_beseg;
	struct m0_cm_sw  *sw;
	char              cm_sw_name[80];
	int               rc;

	rc = m0_cm_sw_store_load(cm, &sw);
	if (rc != 0)
		return M0_RC(rc);
	M0_LOG(M0_DEBUG, "sw = %p", sw);

	sprintf(cm_sw_name, "cm_sw_%llu", (unsigned long long)cm->cm_id);
        if (tx->tx_state < M0_DTX_INIT) {
                m0_dtx_init(tx, seg->bs_domain,
                                &fom->fo_loc->fl_group);
		M0_BE_FREE_CREDIT_PTR(sw, seg, &tx->tx_betx_cred);
		m0_be_seg_dict_delete_credit(seg, cm_sw_name,
					     &tx->tx_betx_cred);
        }

        if (m0_be_tx_state(&tx->tx_betx) == M0_BTS_PREPARE)
                m0_dtx_open(tx);
	if (m0_be_tx_state(&tx->tx_betx) == M0_BTS_FAILED)
		return M0_RC(tx->tx_betx.t_sm.sm_rc);
        if (M0_IN(m0_be_tx_state(&tx->tx_betx), (M0_BTS_OPENING,
						 M0_BTS_GROUPING))) {
                m0_fom_wait_on(fom, &tx->tx_betx.t_sm.sm_chan, &fom->fo_cb);
                return M0_FSO_WAIT;
        } else
		m0_dtx_opened(tx);

	M0_BE_FREE_PTR_SYNC(sw, seg, &tx->tx_betx);
	m0_be_seg_dict_delete(seg, &tx->tx_betx, cm_sw_name);
	m0_fom_wait_on(fom, &tx->tx_betx.t_sm.sm_chan, &fom->fo_cb);
	m0_dtx_done(tx);
	m0_fom_phase_move(fom, rc, SWU_STORE_WAIT);
	return M0_FSO_WAIT;
}

static int (*swu_action[]) (struct m0_cm_sw_update *swu) = {
	[SWU_STORE_INIT]      = swu_store_init,
	[SWU_STORE_INIT_WAIT] = swu_store_init_wait,
	[SWU_UPDATE]          = swu_update,
	[SWU_STORE]           = swu_store,
	[SWU_STORE_WAIT]      = swu_store_wait,
	[SWU_COMPLETE]        = swu_complete,
};

static uint64_t cm_swu_fom_locality(const struct m0_fom *fom)
{
	return fom->fo_type->ft_id;
}
static int cm_swu_fom_tick(struct m0_fom *fom)
{
	struct m0_cm_sw_update *swu;
	int                     phase = m0_fom_phase(fom);
	int                     rc;

	swu = cm_fom2swu(fom);
	rc = swu_action[phase](swu);
	if (rc < 0) {
		m0_fom_phase_move(fom, 0, SWU_FINI);
		rc = M0_FSO_WAIT;
	}

	return M0_RC(rc);
}

static void cm_swu_fom_fini(struct m0_fom *fom)
{
	m0_fom_fini(fom);
}

static const struct m0_fom_ops cm_sw_update_fom_ops = {
	.fo_fini          = cm_swu_fom_fini,
	.fo_tick          = cm_swu_fom_tick,
	.fo_home_locality = cm_swu_fom_locality
};

M0_INTERNAL void m0_cm_sw_update_init(struct m0_cm_type *cmtype)
{
	m0_fom_type_init(&cmtype->ct_swu_fomt, cmtype->ct_fom_id + 1,
			 &cm_sw_update_fom_type_ops,
			 &cmtype->ct_stype, &cm_sw_update_conf);
}

M0_INTERNAL void m0_cm_sw_update_start(struct m0_cm *cm)
{
	struct m0_cm_sw_update *swu = &cm->cm_sw_update;
	struct m0_fom          *fom = &swu->swu_fom;

	swu->swu_is_complete = false;
	m0_fom_init(&cm->cm_sw_update.swu_fom, &cm->cm_type->ct_swu_fomt,
		    &cm_sw_update_fom_ops, NULL, NULL, cm->cm_service.rs_reqh);
	m0_fom_queue(fom, cm->cm_service.rs_reqh);

	M0_LEAVE();
}

#undef M0_TRACE_SUBSYSTEM

/** @} endgroup CMSWFOM */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
