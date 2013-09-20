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

M0_INTERNAL void m0_sns_cm_trigger_fop_fini(struct m0_fop_type *ft)
{
	m0_fop_type_fini(ft);
}

M0_INTERNAL int m0_sns_cm_trigger_fop_init(struct m0_fop_type *ft,
					   enum M0_RPC_OPCODES op,
					   const char *name,
					   const struct m0_xcode_type *xt,
					   uint64_t rpc_flags,
					   struct m0_cm_type *cmt)
{
	m0_sm_conf_extend(m0_generic_conf.scf_state, trigger_phases,
			  m0_generic_conf.scf_nr_states);
	return  M0_FOP_TYPE_INIT(ft,
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
	struct m0_reqh              *reqh;
	struct m0_cm                *cm;
	struct m0_sns_cm            *scm;
	struct m0_fop               *rfop;
	struct trigger_fop          *treq;
	struct trigger_rep_fop      *trep;
	struct m0_fop_type          *ft = NULL;
	//struct m0_clink              tclink;
	int                          i;
	int                          rc;

	if (m0_fom_phase(fom) < M0_FOPH_NR) {
		rc = m0_fom_tick_generic(fom);
	} else {
		reqh = fom->fo_loc->fl_dom->fd_reqh;
		cm = container_of(fom->fo_service, struct m0_cm, cm_service);
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
				m0_fom_wait_on(fom, &scm->sc_wait,
					       &fom->fo_cb);
				m0_mutex_unlock(&scm->sc_wait_mutex);
				/*m0_mutex_lock(&scm->sc_wait_mutex);
				m0_clink_init(&tclink, NULL);
				m0_clink_add(&scm->sc_wait, &tclink);
				m0_mutex_unlock(&scm->sc_wait_mutex);
				m0_fom_block_enter(fom);*/
				rc = m0_cm_ready(cm);
				M0_ASSERT(rc == 0);
				/*if (cm->cm_proxy_nr > 0)
					m0_chan_wait(&tclink);

				m0_fom_block_leave(fom);
				m0_mutex_lock(&scm->sc_wait_mutex);
				m0_clink_del(&tclink);
				m0_mutex_unlock(&scm->sc_wait_mutex);*/
				m0_fom_phase_set(fom, TPH_START_WAIT);
				rc = M0_FSO_WAIT;
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
				if (scm->sc_op == SNS_REPAIR)
					ft = &repair_trigger_rep_fopt;
				else if (scm->sc_op == SNS_REBALANCE)
					ft = &rebalance_trigger_rep_fopt;

				rfop = m0_fop_alloc(ft, NULL);
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
