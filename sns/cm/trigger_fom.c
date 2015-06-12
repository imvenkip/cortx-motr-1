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

extern struct m0_fop_type repair_trigger_fopt;
extern struct m0_fop_type repair_quiesce_trigger_fopt;
extern struct m0_fop_type repair_status_fopt;
extern struct m0_fop_type rebalance_trigger_fopt;
extern struct m0_fop_type rebalance_quiesce_trigger_fopt;
extern struct m0_fop_type rebalance_status_fopt;

extern struct m0_fop_type repair_trigger_rep_fopt;
extern struct m0_fop_type repair_quiesce_trigger_rep_fopt;
extern struct m0_fop_type repair_status_rep_fopt;
extern struct m0_fop_type rebalance_trigger_rep_fopt;
extern struct m0_fop_type rebalance_quiesce_trigger_rep_fopt;
extern struct m0_fop_type rebalance_status_rep_fopt;

static const struct m0_fom_ops trigger_fom_ops = {
	.fo_fini          = trigger_fom_fini,
	.fo_tick          = trigger_fom_tick,
	.fo_home_locality = trigger_fom_home_locality
};

static const struct m0_fom_type_ops trigger_fom_type_ops = {
	.fto_create = trigger_fom_create,
};

enum trigger_phases {
	TPH_PREPARE = M0_FOPH_NR + 1,
	TPH_READY,
	TPH_START,
	TPH_FINI = M0_FOM_PHASE_FINISH
};

static struct m0_sm_state_descr trigger_phases[] = {
	[TPH_PREPARE] = {
		.sd_name      = "Prepare copy machine",
		.sd_allowed   = M0_BITS(TPH_READY, M0_FOPH_FAILURE, M0_FOPH_SUCCESS)
	},
	[TPH_READY] = {
		.sd_name      = "Send ready fops",
		.sd_allowed   = M0_BITS(TPH_START, M0_FOPH_FAILURE)
	},
	[TPH_START] = {
		.sd_name      = "Start sns repair/rebalance",
		.sd_allowed   = M0_BITS(M0_FOPH_SUCCESS, M0_FOPH_FAILURE)
	},
};

const struct m0_sm_conf trigger_conf = {
	.scf_name      = "SNS Trigger",
	.scf_nr_states = ARRAY_SIZE(trigger_phases),
	.scf_state     = trigger_phases
};

M0_INTERNAL void m0_sns_cm_trigger_fop_fini(struct m0_fop_type *ft)
{
	m0_fop_type_fini(ft);
}

M0_INTERNAL void m0_sns_cm_trigger_fop_init(struct m0_fop_type *ft,
					    enum M0_RPC_OPCODES op,
					    const char *name,
					    const struct m0_xcode_type *xt,
					    uint64_t rpc_flags,
					    struct m0_cm_type *cmt)
{
	m0_sm_conf_extend(m0_generic_conf.scf_state, trigger_phases,
			  m0_generic_conf.scf_nr_states);
	M0_FOP_TYPE_INIT(ft,
			 .name      = name,
			 .opcode    = op,
			 .xt        = xt,
			 .rpc_flags = rpc_flags,
			 .fom_ops   = &trigger_fom_type_ops,
			 .svc_type  = &cmt->ct_stype,
			 .sm        = &trigger_conf);
}

static int trigger_fom_create(struct m0_fop *fop, struct m0_fom **out,
			      struct m0_reqh *reqh)
{
	struct m0_fom *fom;
	struct m0_fop *rep_fop = NULL;

	M0_PRE(fop != NULL);
	M0_PRE(out != NULL);

	M0_ALLOC_PTR(fom);
	if (fom == NULL)
		return M0_ERR(-ENOMEM);

	switch (m0_fop_opcode(fop)) {
	case M0_SNS_REPAIR_TRIGGER_OPCODE:
		rep_fop = m0_fop_reply_alloc(fop, &repair_trigger_rep_fopt);
		break;
	case M0_SNS_REPAIR_QUIESCE_OPCODE:
		rep_fop = m0_fop_reply_alloc(fop,
					&repair_quiesce_trigger_rep_fopt);
		break;
	case M0_SNS_REPAIR_STATUS_OPCODE:
		rep_fop = m0_fop_reply_alloc(fop,
					&repair_status_rep_fopt);
		break;
	case M0_SNS_REBALANCE_TRIGGER_OPCODE:
		rep_fop = m0_fop_reply_alloc(fop, &rebalance_trigger_rep_fopt);
		break;
	case M0_SNS_REBALANCE_QUIESCE_OPCODE:
		rep_fop = m0_fop_reply_alloc(fop,
					&rebalance_quiesce_trigger_rep_fopt);
		break;
	case M0_SNS_REBALANCE_STATUS_OPCODE:
		rep_fop = m0_fop_reply_alloc(fop,
					&rebalance_status_rep_fopt);
		break;
	default:
		M0_IMPOSSIBLE("Invalid fop, opcode=%d", m0_fop_opcode(fop));
	}

	m0_fom_init(fom, &fop->f_type->ft_fom_type, &trigger_fom_ops, fop,
		    rep_fop, reqh);

	*out = fom;
	return 0;
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
	int                    rc;

	if (treq->op == SNS_REPAIR_QUIESCE ||
	    treq->op == SNS_REBALANCE_QUIESCE) {
		/* setting quiesce flag to running copy machine and quit */
		cm->cm_quiesce = true;
		m0_fom_phase_set(fom, M0_FOPH_SUCCESS);
		trigger_rep_set(fom);
		return M0_FSO_AGAIN;
	}

	if (treq->op == SNS_REPAIR_STATUS ||
	    treq->op == SNS_REBALANCE_STATUS) {
		struct m0_fop             *rfop = fom->fo_rep_fop;
		struct sns_status_rep_fop *trep = m0_fop_data(rfop);
		static uint64_t            progress;
		enum m0_cm_state           cm_state;
		enum m0_sns_cm_status      cm_status;

		m0_cm_lock(cm);
		cm_state = m0_cm_state_get(cm);
		m0_cm_unlock(cm);
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
		trep->status = cm_status;
		/* TODO: what should be filled as 'progress'? */
		trep->progress = progress++;
		trigger_rep_set(fom);
		return M0_FSO_AGAIN;
	}

	scm->sc_op = treq->op;
	state = scm->sc_op == SNS_REPAIR ? M0_PNDS_SNS_REPAIRING :
					   M0_PNDS_SNS_REBALANCING;
	rc = m0_cm_prepare(cm)?: m0_sns_cm_pm_event_post(scm,
							 &fom->fo_tx.tx_betx,
							 M0_POOL_DEVICE,
							 state);
	if (rc != 0)
		return M0_RC(rc);
	m0_mutex_lock(&cm->cm_wait_mutex);
	m0_fom_wait_on(fom, &cm->cm_ready_wait,
			&fom->fo_cb);
	m0_mutex_unlock(&cm->cm_wait_mutex);
	m0_fom_phase_set(fom, TPH_READY);
	M0_LOG(M0_DEBUG, "got trigger: prepare");

	return M0_FSO_WAIT;
}

static int ready(struct m0_fom *fom)
{
	struct m0_cm *cm = trig2cm(fom);
	int           rc;

	rc = m0_cm_ready(cm);
	if (rc != 0)
		return M0_RC(rc);
	m0_fom_phase_set(fom, TPH_START);
	M0_LOG(M0_DEBUG, "trigger: ready");
	return M0_FSO_AGAIN;
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
	[TPH_PREPARE] = prepare,
	[TPH_READY]   = ready,
	[TPH_START]   = start,
};

static int trigger_fom_tick(struct m0_fom *fom)
{
	struct m0_be_tx_credit *tx_cred;
	struct m0_cm           *cm;
	struct m0_sns_cm       *scm;
	struct m0_poolmach     *pm;
	struct m0_confc        *confc;
	struct m0_mero         *mero;
	uint64_t                nr_fail;
	int                     rc = 0;

	if (m0_fom_phase(fom) < M0_FOPH_NR) {
		if (m0_fom_phase(fom) == M0_FOPH_TXN_OPEN){
			/* Calculate credits for global pool machine. */
			pm = m0_ios_poolmach_get(m0_fom_reqh(fom));
			M0_ASSERT(pm != NULL);
			nr_fail = m0_poolmach_nr_dev_failures(pm);
			tx_cred = m0_fom_tx_credit(fom);
			m0_poolmach_store_credit(pm, tx_cred);
			m0_be_tx_credit_mul(tx_cred, nr_fail);
			m0_be_tx_credit_add(tx_cred, tx_cred);
			cm = trig2cm(fom);
			scm = cm2sns(cm);
			confc = &scm->sc_base.cm_service.rs_reqh->rh_confc;
			mero = m0_cs_ctx_get(scm->sc_base.cm_service.rs_reqh);
			rc = m0_poolmach_credit_calc(pm, confc,
						     &mero->cc_pools_common,
						     tx_cred);
			if (rc != 0)
				goto out;
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
