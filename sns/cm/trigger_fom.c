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

#include "fop/fop.h"
#include "fop/fom.h"
#include "fop/fom_generic.h"

#include "rpc/rpc.h"
#include "fop/fop_item_type.h"

#include "cm/proxy.h"
#include "cm/cm.h"

#include "sns/cm/trigger_fop.h"
#include "sns/cm/cm.h"
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
static void trigger_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc);

extern struct m0_fop_type repair_trigger_fopt;
extern struct m0_fop_type rebalance_trigger_fopt;

extern struct m0_fop_type repair_trigger_rep_fopt;
extern struct m0_fop_type rebalance_trigger_rep_fopt;

static const struct m0_fom_ops trigger_fom_ops = {
	.fo_fini          = trigger_fom_fini,
	.fo_tick          = trigger_fom_tick,
	.fo_home_locality = trigger_fom_home_locality,
	.fo_addb_init     = trigger_fom_addb_init
};

static const struct m0_fom_type_ops trigger_fom_type_ops = {
	.fto_create = trigger_fom_create,
};

enum trigger_phases {
	TPH_PREPARE = M0_FOPH_NR + 1,
	TPH_READY,
	TPH_START,
	TPH_STOP,
	TPH_FINI = M0_FOM_PHASE_FINISH
};

static struct m0_sm_state_descr trigger_phases[] = {
	[TPH_PREPARE] = {
		.sd_name      = "Prepare copy machine",
		.sd_allowed   = M0_BITS(TPH_READY, M0_FOPH_FAILURE)
	},
	[TPH_READY] = {
		.sd_name      = "Send ready fops",
		.sd_allowed   = M0_BITS(TPH_START, M0_FOPH_FAILURE)
	},
	[TPH_START] = {
		.sd_name      = "Start sns repair",
		.sd_allowed   = M0_BITS(TPH_STOP, M0_FOPH_FAILURE)
	},
	[TPH_STOP] = {
		.sd_name      = "Stop sns repair",
		.sd_allowed   = M0_BITS(M0_FOPH_SUCCESS)
	}
};

struct m0_sm_conf trigger_conf = {
	.scf_name      = "Trigger phases",
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

static bool is_repair_trigger_fop(const struct m0_fop *fop)
{
	M0_PRE(fop != NULL);

	return fop->f_type == &repair_trigger_fopt;
}

static bool is_rebalance_trigger_fop(const struct m0_fop *fop)
{
	M0_PRE(fop != NULL);

	return fop->f_type == &rebalance_trigger_fopt;
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
		return -ENOMEM;
	if (is_repair_trigger_fop(fop))
		rep_fop = m0_fop_reply_alloc(fop, &repair_trigger_rep_fopt);
	else if (is_rebalance_trigger_fop(fop))
		rep_fop = m0_fop_reply_alloc(fop, &rebalance_trigger_rep_fopt);
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
}
static struct m0_cm *trig2cm(const struct m0_fom *fom)
{
	return container_of(fom->fo_service, struct m0_cm, cm_service);
}

static int prepare(struct m0_fom *fom)
{
	struct m0_cm       *cm = trig2cm(fom);
	struct m0_sns_cm   *scm = cm2sns(cm);
	struct trigger_fop *treq = m0_fop_data(fom->fo_fop);
	int                 rc;

	scm->sc_op = treq->op;
	/*
	 * To handle blocking pool machine state
	 * transitions.
	 */
	m0_fom_block_enter(fom);
	rc = m0_cm_prepare(cm);
	m0_fom_block_leave(fom);
	if (rc != 0)
		return rc;
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

	m0_mutex_lock(&cm->cm_wait_mutex);
	m0_fom_wait_on(fom, &cm->cm_ready_wait,
		       &fom->fo_cb);
	m0_mutex_unlock(&cm->cm_wait_mutex);
	rc = m0_cm_ready(cm);
	if (rc != 0)
		return rc;
	m0_fom_phase_set(fom, TPH_START);
	if (cm->cm_proxy_nr > 1)
		rc = M0_FSO_WAIT;
	else {
		m0_mutex_lock(&cm->cm_wait_mutex);
		m0_fom_callback_cancel(&fom->fo_cb);
		m0_mutex_unlock(&cm->cm_wait_mutex);
		rc = M0_FSO_AGAIN;
	}
	M0_LOG(M0_DEBUG, "trigger: ready");
	return rc;
}

static int start(struct m0_fom *fom)
{
	struct m0_cm *cm = trig2cm(fom);
	int           rc;

	m0_mutex_lock(&cm->cm_wait_mutex);
	m0_fom_wait_on(fom, &cm->cm_complete_wait,
		       &fom->fo_cb);
	m0_mutex_unlock(&cm->cm_wait_mutex);
	rc = m0_cm_start(cm);
	if (rc != 0)
		return rc;
	m0_fom_phase_set(fom, TPH_STOP);
	M0_LOG(M0_DEBUG, "trigger: start");
	return M0_FSO_WAIT;
}

static int stop(struct m0_fom *fom)
{
	struct m0_cm *cm = trig2cm(fom);

	m0_fom_block_enter(fom);
	trigger_rep_set(fom);
	m0_cm_stop(cm);
	m0_fom_block_leave(fom);
	m0_fom_phase_set(fom, M0_FOPH_SUCCESS);

	M0_LOG(M0_DEBUG, "trigger: stop");
	return M0_FSO_AGAIN;
}

static int (*trig_action[]) (struct m0_fom *) = {
	[TPH_PREPARE] = prepare,
	[TPH_READY]   = ready,
	[TPH_START]   = start,
	[TPH_STOP]    = stop,
};

static int trigger_fom_tick(struct m0_fom *fom)
{
	int rc;

	if (m0_fom_phase(fom) < M0_FOPH_NR) {
		rc = m0_fom_tick_generic(fom);
	} else
		rc = trig_action[m0_fom_phase(fom)](fom);

	if (rc < 0) {
		m0_fom_phase_move(fom, rc, M0_FOPH_FAILURE);
		trigger_rep_set(fom);
		rc = M0_FSO_AGAIN;
	}

	return rc;
}

static void trigger_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	/**
	 * @todo: Do the actual impl, need to set MAGIC, so that
	 * m0_fom_init() can pass
	 */
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
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
