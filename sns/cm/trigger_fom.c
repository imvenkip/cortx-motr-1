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

#include "fop/fop.h"
#include "fop/fom.h"
#include "fop/fom_generic.h"

#include "rpc/rpc.h"
#include "fop/fop_item_type.h"

#include "cm/proxy.h"

#include "sns/cm/trigger_fop.h"
#include "sns/cm/trigger_fop_xc.h"
#include "rpc/rpc_opcodes.h"

#include "sns/cm/cm.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/trace.h"

/*
 * Implements a simplistic sns repair trigger FOM for corresponding trigger FOP.
 * This is solely for testing purpose and a separate trigger FOP/FOM will be
 * implemented later, which would be similar to this one.
 */

struct m0_fop_type trigger_fop_fopt;
struct m0_fop_type trigger_rep_fop_fopt;

static int trigger_fom_tick(struct m0_fom *fom);
static int trigger_fom_create(struct m0_fop *fop, struct m0_fom **out,
			      struct m0_reqh *reqh);
static void trigger_fom_fini(struct m0_fom *fom);
static size_t trigger_fom_home_locality(const struct m0_fom *fom);
static void trigger_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc);

static const struct m0_fom_ops trigger_fom_ops = {
	.fo_fini          = trigger_fom_fini,
	.fo_tick          = trigger_fom_tick,
	.fo_home_locality = trigger_fom_home_locality,
	.fo_addb_init     = trigger_fom_addb_init
};

static const struct m0_fom_type_ops trigger_fom_type_ops = {
	.fto_create = trigger_fom_create,
};

void m0_sns_repair_trigger_fop_fini(void)
{
	m0_fop_type_fini(&trigger_fop_fopt);
	m0_fop_type_fini(&trigger_rep_fop_fopt);
	m0_xc_trigger_fop_fini();
}

enum trigger_phases {
	TPH_READY = M0_FOPH_NR + 1,
	TPH_START_WAIT,
	TPH_STOP_WAIT
};

static struct m0_sm_state_descr trigger_phases[] = {
	[TPH_READY] = {
		.sd_name      = "Send ready fops",
		.sd_allowed   = M0_BITS(TPH_START_WAIT)
	},
	[TPH_START_WAIT] = {
		.sd_name      = "Start sns repair",
		.sd_allowed   = M0_BITS(TPH_STOP_WAIT)
	},
	[TPH_STOP_WAIT] = {
		.sd_name      = "Stop sns repair",
		.sd_allowed   = M0_BITS(M0_FOPH_SUCCESS)
	}
};

struct m0_sm_conf trigger_conf = {
	.scf_name      = "Trigger phases",
	.scf_nr_states = ARRAY_SIZE(trigger_phases),
	.scf_state     = trigger_phases
};

int m0_sns_repair_trigger_fop_init(void)
{
	struct m0_reqh_service_type *stype;

	stype = m0_reqh_service_type_find("sns_cm");
	m0_xc_trigger_fop_init();
	m0_sm_conf_extend(m0_generic_conf.scf_state, trigger_phases,
			  m0_generic_conf.scf_nr_states);
	return  M0_FOP_TYPE_INIT(&trigger_fop_fopt,
			.name      = "sns repair trigger",
			.opcode    = M0_SNS_REPAIR_TRIGGER_OPCODE,
			.xt        = trigger_fop_xc,
			.rpc_flags = M0_RPC_ITEM_TYPE_REQUEST |
				     M0_RPC_ITEM_TYPE_MUTABO,
			.fom_ops   = &trigger_fom_type_ops,
			.svc_type  = stype,
			.sm        = &trigger_conf) ?:
		M0_FOP_TYPE_INIT(&trigger_rep_fop_fopt,
				.name      = "sns repair trigger reply",
				.opcode    = M0_SNS_REPAIR_TRIGGER_REP_OPCODE,
				.xt        = trigger_rep_fop_xc,
				.rpc_flags = M0_RPC_ITEM_TYPE_REPLY);

}


static int trigger_fom_create(struct m0_fop *fop, struct m0_fom **out,
			      struct m0_reqh *reqh)
{
	struct m0_fom *fom;

	M0_PRE(fop != NULL);
	M0_PRE(out != NULL);

	M0_ALLOC_PTR(fom);
	if (fom == NULL)
		return -ENOMEM;

	m0_fom_init(fom, &fop->f_type->ft_fom_type, &trigger_fom_ops, fop, NULL,
		    reqh, fop->f_type->ft_fom_type.ft_rstype);

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

static int trigger_fom_tick(struct m0_fom *fom)
{
	int                          i;
	int                          rc;
	struct m0_reqh              *reqh;
	struct m0_cm                *cm;
	struct m0_sns_cm            *scm;
	struct m0_reqh_service      *service;
	struct m0_reqh_service_type *stype;
	struct m0_fop               *rfop;
	struct trigger_fop          *treq;
	struct trigger_rep_fop      *trep;
	struct m0_clink              tclink;

	if (m0_fom_phase(fom) < M0_FOPH_NR) {
		rc = m0_fom_tick_generic(fom);
	} else {
		reqh = fom->fo_loc->fl_dom->fd_reqh;
		stype = m0_reqh_service_type_find("sns_cm");
		service = m0_reqh_service_find(stype, reqh);
		M0_ASSERT(service != NULL);
		cm = container_of(service, struct m0_cm, cm_service);
		scm = cm2sns(cm);
		M0_LOG(M0_DEBUG, "start state = %d", m0_fom_phase(fom));
		switch(m0_fom_phase(fom)) {
			case TPH_READY:
				treq = m0_fop_data(fom->fo_fop);
				M0_ALLOC_ARR(scm->sc_it.si_fdata,
						treq->fdata.fd_nr);
				M0_ASSERT(scm->sc_it.si_fdata != NULL);
				for (i = 0; i < treq->fdata.fd_nr; ++i)
					scm->sc_it.si_fdata[i] =
						treq->fdata.fd_index[i];
				scm->sc_failures_nr += treq->fdata.fd_nr;
				scm->sc_op             = treq->op;
				m0_mutex_lock(&scm->sc_wait_mutex);
				m0_clink_init(&tclink, NULL);
				m0_clink_add(&scm->sc_wait, &tclink);
				m0_mutex_unlock(&scm->sc_wait_mutex);
				m0_fom_block_enter(fom);
				rc = m0_cm_ready(cm);
				M0_ASSERT(rc == 0);
				if (cm->cm_proxy_nr > 0)
					/* Wait for READY phase to complete. */
					m0_chan_wait(&tclink);

				m0_fom_block_leave(fom);
				m0_mutex_lock(&scm->sc_wait_mutex);
				m0_clink_del(&tclink);
				m0_mutex_unlock(&scm->sc_wait_mutex);
				m0_fom_phase_set(fom, TPH_START_WAIT);
				rc = M0_FSO_AGAIN;
				M0_LOG(M0_DEBUG, "got trigger: ready done");
				break;
			case TPH_START_WAIT:
				m0_mutex_lock(&scm->sc_wait_mutex);
				m0_fom_wait_on(fom, &scm->sc_wait,
					       &fom->fo_cb);
				m0_mutex_unlock(&scm->sc_wait_mutex);
				rc = m0_cm_start(cm);
				M0_ASSERT(rc == 0);
				m0_fom_phase_set(fom, TPH_STOP_WAIT);
				rc = M0_FSO_WAIT;
				M0_LOG(M0_DEBUG, "got trigger: start done");
				break;
			case TPH_STOP_WAIT:
				m0_fom_block_enter(fom);
				rfop = m0_fop_alloc(&trigger_rep_fop_fopt,
						    NULL);
				if (rfop == NULL) {
					m0_fom_phase_set(fom, M0_FOPH_FINISH);
					return M0_FSO_WAIT;
				}
				trep = m0_fop_data(rfop);
				trep->rc = m0_fom_rc(fom);
				fom->fo_rep_fop = rfop;
				m0_cm_stop(&scm->sc_base);
				m0_fom_block_leave(fom);
				m0_fom_phase_set(fom, M0_FOPH_SUCCESS);
				rc = M0_FSO_AGAIN;
				M0_LOG(M0_DEBUG, "got trigger: wait done");
				break;
			default:
				M0_IMPOSSIBLE("Invalid fop");
				rc = -EINVAL;
				break;
		}
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
