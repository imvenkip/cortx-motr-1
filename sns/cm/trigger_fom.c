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
 * Original creation date: 09/11/2011
 */

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/misc.h"           /* M0_IN() */
#include  "lib/locality.h"
#include "lib/finject.h"

#include "mero/setup.h"
#include "conf/diter.h"
#include "conf/obj_ops.h"
#include "fop/fop.h"
#include "fop/fom.h"
#include "fop/fom_generic.h"

#include "rpc/rpc.h"
#include "fop/fop_item_type.h"

#include "ioservice/io_device.h" /* m0_ios_poolmach_get */

#include "cm/proxy.h"
#include "cm/cm.h"

#include "sns/cm/trigger_fop.h"
#include "sns/cm/trigger_fom.h"
#include "sns/cm/cm.h"

#include "pool/pool_machine.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/trace.h"

/*
 * Implements a simplistic sns repair trigger FOM for corresponding trigger FOP.
 * This is solely for testing purpose and a separate trigger FOP/FOM will be
 * implemented later, which would be similar to this one.
 */

static int trigger_fom_tick(struct m0_fom *fom);
static int trigger_fom_create(struct m0_fop *fop, struct m0_fom **out,
			      struct m0_reqh *reqh);
static void trigger_fom_fini(struct m0_fom *fom);
static size_t trigger_fom_home_locality(const struct m0_fom *fom);

static const struct m0_fom_ops trigger_fom_ops = {
	.fo_fini          = trigger_fom_fini,
	.fo_tick          = trigger_fom_tick,
	.fo_home_locality = trigger_fom_home_locality
};

const struct m0_fom_type_ops m0_sns_trigger_fom_type_ops = {
	.fto_create = trigger_fom_create,
};

struct m0_sm_state_descr m0_sns_trigger_phases[] = {
	[M0_SNS_TPH_PREPARE] = {
		.sd_name      = "Prepare copy machine",
		.sd_allowed   = M0_BITS(M0_SNS_TPH_READY, M0_FOPH_INIT,
					M0_FOPH_FAILURE, M0_FOPH_SUCCESS)
	},
	[M0_SNS_TPH_READY] = {
		.sd_name      = "Send ready fops",
		.sd_allowed   = M0_BITS(M0_SNS_TPH_START, M0_FOPH_FAILURE)
	},
	[M0_SNS_TPH_START] = {
		.sd_name      = "Start sns repair/rebalance",
		.sd_allowed   = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE)
	},
};

const struct m0_sm_conf m0_sns_trigger_conf = {
	.scf_name      = "SNS Trigger",
	.scf_nr_states = ARRAY_SIZE(m0_sns_trigger_phases),
	.scf_state     = m0_sns_trigger_phases
};

static int trigger_fom_create(struct m0_fop *fop, struct m0_fom **out,
			      struct m0_reqh *reqh)
{
	struct m0_fop_type *sns_fop_type[] = {
		[M0_SNS_REPAIR_TRIGGER_OPCODE] =
			&m0_sns_repair_trigger_rep_fopt,
		[M0_SNS_REPAIR_QUIESCE_OPCODE] =
			&m0_sns_repair_quiesce_trigger_rep_fopt,
		[M0_SNS_REPAIR_STATUS_OPCODE] =
			&m0_sns_repair_status_rep_fopt,
		[M0_SNS_REBALANCE_TRIGGER_OPCODE] =
			&m0_sns_rebalance_trigger_rep_fopt,
		[M0_SNS_REBALANCE_QUIESCE_OPCODE] =
			&m0_sns_rebalance_quiesce_trigger_rep_fopt,
		[M0_SNS_REBALANCE_STATUS_OPCODE] =
			&m0_sns_rebalance_status_rep_fopt,
		[M0_SNS_REPAIR_ABORT_OPCODE] =
			&m0_sns_repair_abort_rep_fopt,
	};
	struct m0_fom       *fom;
	struct m0_fop       *rep_fop;
	uint32_t             op;

	M0_PRE(fop != NULL);
	M0_PRE(out != NULL);

	op = m0_fop_opcode(fop);
	M0_ASSERT(IS_IN_ARRAY(op, sns_fop_type));
	M0_ALLOC_PTR(fom);
	rep_fop = m0_fop_reply_alloc(fop, sns_fop_type[op]);
	if (fom == NULL || rep_fop == NULL)
		goto err;

	m0_fom_init(fom, &fop->f_type->ft_fom_type, &trigger_fom_ops,
		    fop, rep_fop, reqh);

	*out = fom;
	return M0_RC(0);

err:
	m0_free(fom);
	m0_free(rep_fop);
	return M0_ERR(-ENOMEM);
}

static void trigger_fom_fini(struct m0_fom *fom)
{
	M0_PRE(fom != NULL);

	m0_fom_fini(fom);
	m0_free(fom);
}

static size_t trigger_fom_home_locality(const struct m0_fom *fom)
{
	M0_PRE(fom != NULL);

	return m0_fop_opcode(fom->fo_fop);
}

static void trigger_rep_set(struct m0_fom *fom)
{
	struct m0_fop          *rfop = fom->fo_rep_fop;
	struct trigger_rep_fop *trep = m0_fop_data(rfop);

	trep->rc = m0_fom_rc(fom);
	fom->fo_rep_fop = rfop;
	M0_LOG(M0_DEBUG, "sending back trigger reply:%d", trep->rc);
}
static struct m0_cm *trig2cm(const struct m0_fom *fom)
{
	return container_of(fom->fo_service, struct m0_cm, cm_service);
}

static int prepare(struct m0_fom *fom)
{
	struct m0_cm          *cm = trig2cm(fom);
	struct m0_sns_cm      *scm = cm2sns(cm);
	struct trigger_fop    *treq = m0_fop_data(fom->fo_fop);
	enum m0_pool_nd_state  state;
	enum m0_cm_state       cm_state;
	int                    rc;

	if (M0_IN(treq->op, (SNS_REPAIR_QUIESCE, SNS_REBALANCE_QUIESCE))) {
		/* setting quiesce flag to running copy machine and quit */
		cm->cm_quiesce = true;
		m0_fom_phase_set(fom, M0_FOPH_SUCCESS);
		trigger_rep_set(fom);
		return M0_FSO_AGAIN;
	}

	cm_state = m0_cm_state_get(cm);

	if (treq->op == SNS_REPAIR_ABORT) {
		/* setting abort flag */
		cm->cm_abort = true;
		M0_LOG(M0_DEBUG, "GOT ABORT cmd");
		m0_fom_phase_set(fom, M0_FOPH_SUCCESS);
		trigger_rep_set(fom);
		return M0_FSO_AGAIN;
	}

	if (M0_IN(treq->op, (SNS_REPAIR_STATUS, SNS_REBALANCE_STATUS))) {
		struct m0_fop                *rfop = fom->fo_rep_fop;
		struct m0_sns_status_rep_fop *trep = m0_fop_data(rfop);
		static uint64_t               progress;
		enum m0_sns_cm_status         cm_status;

		/* sending back status and progress */
		M0_LOG(M0_DEBUG, "sending back status for %d: cm state=%d",
				 treq->op, cm_state);

		switch (cm_state) {
			case M0_CMS_IDLE:
			case M0_CMS_STOP:
				if (cm->cm_quiesce)
					cm_status = SNS_CM_STATUS_PAUSED;
				else
					cm_status = SNS_CM_STATUS_IDLE;
				break;
			case M0_CMS_PREPARE:
			case M0_CMS_READY:
			case M0_CMS_ACTIVE:
				cm_status = SNS_CM_STATUS_STARTED;
				break;
			case M0_CMS_FAIL:
				cm_status = SNS_CM_STATUS_FAILED;
				break;
			case M0_CMS_FINI:
			case M0_CMS_INIT:
			default:
				cm_status = SNS_CM_STATUS_INVALID;
				break;
		}

		m0_fom_phase_set(fom, M0_FOPH_SUCCESS);
		trep->ssr_state = cm_status;
		/* TODO: what should be filled as 'progress'? */
		trep->ssr_progress = progress++;
		trigger_rep_set(fom);
		return M0_FSO_AGAIN;
	}

	scm->sc_op = treq->op;
	state = scm->sc_op == SNS_REPAIR ? M0_PNDS_SNS_REPAIRING :
					   M0_PNDS_SNS_REBALANCING;
	if (cm_state == M0_CMS_IDLE) {
		m0_cm_wait(cm, fom);
		rc = m0_cm_prepare(cm);
		if (rc == 0) {
			m0_fom_phase_set(fom, M0_FOPH_INIT);
			rc = M0_FSO_WAIT;
		} else {
			m0_cm_wait_cancel(cm, fom);
		}
	} else {
		M0_ASSERT(fom->fo_tx.tx_state == M0_DTX_OPEN);
		rc = m0_sns_cm_pm_event_post(scm, &fom->fo_tx.tx_betx,
					     M0_POOL_DEVICE, state);
		if (rc == 0) {
			m0_fom_phase_set(fom, M0_SNS_TPH_READY);
			rc = M0_FSO_AGAIN;
		}
	}
	M0_LOG(M0_DEBUG, "got trigger: prepare");

	return M0_RC(rc);
}

static int ready(struct m0_fom *fom)
{
	struct m0_cm *cm = trig2cm(fom);
	int           rc;

	if (M0_FI_ENABLED("no_wait")) {
		rc = m0_cm_ready(cm);
		if (rc == 0) {
			m0_fom_phase_set(fom, M0_SNS_TPH_START);
			rc = M0_FSO_AGAIN;
		}
		return M0_RC(rc);
	}
	m0_cm_proxies_init_wait(cm, fom);
	rc = m0_cm_ready(cm);
	if (rc != 0) {
		m0_cm_wait_cancel(cm, fom);
		return M0_ERR(rc);
	}
	m0_fom_phase_set(fom, M0_SNS_TPH_START);
	M0_LOG(M0_DEBUG, "trigger: ready");
	return M0_FSO_WAIT;
}

static int start(struct m0_fom *fom)
{
	struct m0_cm *cm = trig2cm(fom);
	int           rc;

	rc = m0_cm_start(cm);
	if (rc != 0)
		return M0_RC(rc);
	M0_LOG(M0_DEBUG, "trigger: start");
	trigger_rep_set(fom);
	m0_fom_phase_set(fom, M0_FOPH_SUCCESS);
	return M0_FSO_AGAIN;
}

static int (*trig_action[]) (struct m0_fom *) = {
	[M0_SNS_TPH_PREPARE] = prepare,
	[M0_SNS_TPH_READY]   = ready,
	[M0_SNS_TPH_START]   = start,
};

static int trigger_fom_tick(struct m0_fom *fom)
{
	struct trigger_fop     *treq = m0_fop_data(fom->fo_fop);
	struct m0_be_tx_credit *tx_cred;
	struct m0_cm           *cm;
	struct m0_sns_cm       *scm;
	struct m0_poolmach     *pm;
	struct m0_confc        *confc;
	struct m0_mero         *mero;
	enum m0_cm_state        cm_state;
	uint64_t                nr_fail;
	int                     rc = 0;

	if (m0_fom_phase(fom) < M0_FOPH_NR) {
		cm = trig2cm(fom);
		if (m0_fom_phase(fom) == M0_FOPH_TXN_OPEN &&
		    M0_IN(treq->op, (SNS_REPAIR, SNS_REBALANCE))){
			/* Calculate credits for global pool machine. */
			pm = m0_ios_poolmach_get(m0_fom_reqh(fom));
			M0_ASSERT(pm != NULL);
			nr_fail = m0_poolmach_nr_dev_failures(pm);
			tx_cred = m0_fom_tx_credit(fom);
			m0_poolmach_store_credit(pm, tx_cred);
			m0_be_tx_credit_mul(tx_cred, nr_fail);
			m0_be_tx_credit_add(tx_cred, tx_cred);
			scm = cm2sns(cm);
			confc = &scm->sc_base.cm_service.rs_reqh->rh_confc;
			mero = m0_cs_ctx_get(scm->sc_base.cm_service.rs_reqh);
			rc = m0_poolmach_credit_calc(pm, confc,
						     &mero->cc_pools_common,
						     tx_cred);
			if (rc != 0)
				goto out;
		} else if (m0_fom_phase(fom) == M0_FOPH_INIT &&
			   M0_IN(treq->op, (SNS_REPAIR, SNS_REBALANCE))) {
			m0_cm_lock(cm);
			cm_state = m0_cm_state_get(cm);
			m0_cm_unlock(cm);
			/*
			 * Run TPH_PREPARE phase before generic phases. This is
			 * required to prevent dependency between trigger_fom's
			 * and m0_cm_sw_update fom's transactions.
			 */
			if (cm_state == M0_CMS_IDLE) {
				m0_fom_phase_set(fom, M0_SNS_TPH_PREPARE);
				return M0_FSO_AGAIN;
			}
		}
		rc = m0_fom_tick_generic(fom);
	} else
		rc = trig_action[m0_fom_phase(fom)](fom);
out:
	if (rc < 0) {
		m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
		trigger_rep_set(fom);
		rc = M0_FSO_AGAIN;
	}

	return M0_RC(rc);
}

#undef M0_TRACE_SUBSYSTEM
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
