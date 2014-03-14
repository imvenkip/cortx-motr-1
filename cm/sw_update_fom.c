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

#include "cm/sw.h"
#include "cm/cm.h"

/**
   @addtogroup CM

   Implementation of sliding window update FOM.
   Provides mechanism to handle blocking operations like local sliding
   update and updating the persistent store with new sliding window.
   Provides interfaces to start, wakeup (if idle) and stop the sliding
   window update FOM.

   @{
*/

enum cm_sw_update_fom_phase {
	SWU_UPDATE = M0_FOM_PHASE_INIT,
	SWU_FINI   = M0_FOM_PHASE_FINISH,
	SWU_STORE,
	SWU_WAIT,
	SWU_COMPLETE,
	SWU_NR
};

static const struct m0_fom_type_ops cm_sw_update_fom_type_ops = {
	.fto_create = NULL
};

static struct m0_fom_type cm_sw_update_fom_type;

static struct m0_sm_state_descr cm_sw_update_sd[SWU_NR] = {
	[SWU_UPDATE] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "Sliding window update",
		.sd_allowed = M0_BITS(SWU_STORE, SWU_COMPLETE, SWU_FINI)
	},
	[SWU_STORE] = {
		.sd_flags   = 0,
		.sd_name    = "sliding window persistent store",
		.sd_allowed = M0_BITS(SWU_WAIT, SWU_FINI)
	},
	[SWU_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "sliding window update fom wait",
		.sd_allowed = M0_BITS(SWU_UPDATE, SWU_FINI)
	},
	[SWU_COMPLETE] = {
		.sd_flags   = 0,
		.sd_name    = "sliding window update complete",
		.sd_allowed = M0_BITS(SWU_WAIT, SWU_FINI)
	},
	[SWU_FINI] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "sliding fom fini",
		.sd_allowed = 0
	},
};

static struct m0_sm_conf cm_sw_update_conf = {
	.scf_name      = "sm: sw update conf",
	.scf_nr_states = ARRAY_SIZE(cm_sw_update_sd),
	.scf_state     = cm_sw_update_sd
};

static struct m0_cm *cm_swu2cm(struct m0_cm_sw_update *swu)
{
	return container_of(swu, struct m0_cm, cm_sw_update);
}

static struct m0_cm_sw_update *cm_fom2swu(struct m0_fom *fom)
{
	return container_of(fom, struct m0_cm_sw_update, swu_fom);
}

static int swu_update(struct m0_cm_sw_update *swu)
{
	struct m0_cm  *cm = cm_swu2cm(swu);
	struct m0_fom *fom = &swu->swu_fom;
	int            rc = M0_FSO_AGAIN;

	M0_PRE(m0_cm_is_locked(cm));
	rc = m0_cm_sw_local_update(cm);
	if (rc == 0 || rc == -ENOSPC) {
		m0_cm_ready_done(cm);
		m0_fom_phase_move(fom, 0, SWU_STORE);
		rc = M0_FSO_AGAIN;
	} else {
		if (rc == -ENOENT) {
			m0_cm_ready_done(cm);
			m0_fom_phase_move(fom, 0, SWU_COMPLETE);
			rc = M0_FSO_AGAIN;
		} else {
			m0_fom_phase_move(fom, rc, SWU_FINI);
			rc = M0_FSO_WAIT;
		}
	}

	return rc;
}

static int swu_store(struct m0_cm_sw_update *swu)
{
	struct m0_cm             *cm = cm_swu2cm(swu);
	struct m0_fom            *fom = &swu->swu_fom;
	struct m0_dtx            *tx = &fom->fo_tx;
	struct m0_be_seg         *seg  = cm->cm_service.rs_reqh->rh_beseg;
	struct m0_cm_aggr_group  *hi;
	struct m0_cm_aggr_group  *lo;
	struct m0_cm_sw           sw;
	int                       rc;

	M0_PRE(m0_cm_is_locked(cm));

	if (tx->tx_state < M0_DTX_INIT) {
		m0_dtx_init(tx, seg->bs_domain,
				&fom->fo_loc->fl_group);
		tx->tx_betx_cred = M0_BE_TX_CREDIT_TYPE(struct m0_cm_sw);
	}

	if (m0_be_tx_state(&tx->tx_betx) == M0_BTS_PREPARE) {
		m0_dtx_open(tx);
		return M0_FSO_AGAIN;
	} else if (m0_be_tx_state(&tx->tx_betx) == M0_BTS_OPENING) {
		m0_fom_wait_on(fom, &tx->tx_betx.t_sm.sm_chan, &fom->fo_cb);
		return M0_FSO_WAIT;
	}

	M0_SET0(&sw);
	hi = m0_cm_ag_hi(cm);
	lo = m0_cm_ag_lo(cm);
	if (hi == NULL && lo == NULL) {
		rc = -EINVAL;
		goto err;
	}
	m0_cm_sw_set(&sw, &lo->cag_id, &hi->cag_id);
	m0_dtx_opened(tx);
	rc = m0_cm_sw_store_update(cm, &tx->tx_betx, &sw);
	if (rc == 0) {
		m0_fom_wait_on(fom, &tx->tx_betx.t_sm.sm_chan, &fom->fo_cb);
		m0_dtx_done(tx);
		m0_fom_phase_move(fom, rc, SWU_WAIT);
	}

err:
	if (rc != 0)
		m0_fom_phase_move(fom, rc, SWU_FINI);

	return M0_FSO_WAIT;
}

static int swu_wait(struct m0_cm_sw_update *swu)
{
	struct m0_cm   *cm = cm_swu2cm(swu);
	struct m0_fom  *fom = &swu->swu_fom;
	struct m0_dtx  *tx = &fom->fo_tx;

	M0_PRE(m0_cm_is_locked(cm));

	if (tx->tx_state == M0_DTX_DONE) {
		if (m0_be_tx_state(&tx->tx_betx) != M0_BTS_DONE) {
			m0_fom_wait_on(fom, &tx->tx_betx.t_sm.sm_chan,
				       &fom->fo_cb);
			goto out;
		}
		m0_dtx_fini(tx);
		M0_SET0(tx);
		if (swu->swu_is_complete)
			m0_fom_phase_move(fom, 0, SWU_FINI);
		else {
			m0_fom_phase_move(fom, 0, SWU_UPDATE);
			swu->swu_is_idle = true;
		}
	}
out:
	return M0_FSO_WAIT;
}

static int swu_complete(struct m0_cm_sw_update *swu)
{
	struct m0_cm             *cm = cm_swu2cm(swu);
	struct m0_fom            *fom = &swu->swu_fom;
	struct m0_dtx            *tx = &fom->fo_tx;
	struct m0_be_seg         *seg  = cm->cm_service.rs_reqh->rh_beseg;
	struct m0_cm_sw          *sw;
	char                      cm_sw_name[80];
	int                       rc;

	M0_PRE(m0_cm_is_locked(cm));

	sprintf(cm_sw_name, "cm_sw_%llu", (unsigned long long)cm->cm_id);
	rc = m0_be_seg_dict_lookup(seg, cm_sw_name, (void**)&sw);
	if (rc != 0) {
		m0_fom_phase_move(fom, 0, SWU_FINI);
		return M0_FSO_WAIT;
	}
	M0_LOG(M0_DEBUG, "sw = %p", sw);
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
		M0_RETURN(tx->tx_betx.t_sm.sm_rc);
        if (m0_be_tx_state(&tx->tx_betx) == M0_BTS_OPENING) {
                m0_fom_wait_on(fom, &tx->tx_betx.t_sm.sm_chan, &fom->fo_cb);
                return M0_FSO_WAIT;
        } else
		m0_dtx_opened(tx);

	M0_BE_FREE_PTR_SYNC(sw, seg, &tx->tx_betx);
	m0_be_seg_dict_delete(seg, &tx->tx_betx, cm_sw_name);
	m0_fom_wait_on(fom, &tx->tx_betx.t_sm.sm_chan, &fom->fo_cb);
	swu->swu_is_complete = true;
	m0_dtx_done(tx);
	m0_fom_phase_move(fom, rc, SWU_WAIT);

	return M0_FSO_WAIT;
}

static int (*swu_action[]) (struct m0_cm_sw_update *swu) = {
	[SWU_UPDATE]   = swu_update,
	[SWU_STORE]    = swu_store,
	[SWU_WAIT]     = swu_wait,
	[SWU_COMPLETE] = swu_complete,
};

static uint64_t cm_swu_fom_locality(const struct m0_fom *fom)
{
	return 0;
}

static int cm_swu_fom_tick(struct m0_fom *fom)
{
	struct m0_cm           *cm;
	struct m0_cm_sw_update *swu;
	int                     phase = m0_fom_phase(fom);
	int                     rc;

	swu = cm_fom2swu(fom);
	cm = cm_swu2cm(swu);
	m0_cm_lock(cm);
	rc = swu_action[phase](swu);
	m0_cm_unlock(cm);

	return rc;
}

static void cm_swu_fom_fini(struct m0_fom *fom)
{
	m0_fom_fini(fom);
}

static void cm_swu_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

static const struct m0_fom_ops cm_sw_update_fom_ops = {
	.fo_fini          = cm_swu_fom_fini,
	.fo_tick          = cm_swu_fom_tick,
	.fo_home_locality = cm_swu_fom_locality,
	.fo_addb_init     = cm_swu_fom_addb_init
};

M0_INTERNAL void m0_cm_sw_update_init(void)
{
	m0_fom_type_init(&cm_sw_update_fom_type, &cm_sw_update_fom_type_ops,
			 NULL, &cm_sw_update_conf);
}

M0_INTERNAL void m0_cm_sw_update_start(struct m0_cm *cm)
{
	struct m0_fom *fom = &cm->cm_sw_update.swu_fom;

	cm->cm_sw_update.swu_is_complete = false;
	m0_fom_init(fom, &cm_sw_update_fom_type, &cm_sw_update_fom_ops, NULL,
		    NULL, cm->cm_service.rs_reqh, cm->cm_service.rs_type);
	m0_fom_queue(fom, cm->cm_service.rs_reqh);

	M0_LEAVE();
}

static void sw_update_fom_wakeup(struct m0_cm_sw_update *swu)
{
	if (swu->swu_is_idle) {
		swu->swu_is_idle = false;
		m0_fom_wakeup(&swu->swu_fom);
	}
}

M0_INTERNAL void m0_cm_sw_update_continue(struct m0_cm *cm)
{
	M0_PRE(m0_cm_is_locked(cm));

	sw_update_fom_wakeup(&cm->cm_sw_update);
}

M0_INTERNAL void m0_cm_sw_update_stop(struct m0_cm *cm)
{
	struct m0_cm_sw_update *swu = &cm->cm_sw_update;

	M0_PRE(m0_cm_is_locked(cm));
	M0_PRE(swu->swu_is_complete);
}

#undef M0_TRACE_SUBSYSTEM

/** @} endgroup CM */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
