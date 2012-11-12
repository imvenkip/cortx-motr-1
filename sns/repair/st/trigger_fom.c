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
#include "lib/misc.h"           /* C2_IN() */
#include "fop/fop.h"
#include "fop/fom.h"
#include "fop/fom_generic.h"

#include "rpc/rpc.h"
#include "fop/fop_item_type.h"

#include "trigger_fop_ff.h"
#include "rpc/rpc_opcodes.h"

#include "sns/repair/cm.h"

/*
 * Implements a simplistic sns repair trigger FOM for corresponding trigger FOP.
 * This is solely for testing purpose and a separate trigger FOP/FOM will be
 * implemented later, which would be similar to this one.
 */

struct c2_fop_type trigger_fop_fopt;
struct c2_fop_type trigger_rep_fop_fopt;

static int trigger_fom_tick(struct c2_fom *fom);
static int trigger_fom_create(struct c2_fop *fop, struct c2_fom **out);
static void trigger_fom_fini(struct c2_fom *fom);
static size_t trigger_fom_home_locality(const struct c2_fom *fom);

static const struct c2_fom_ops trigger_fom_ops = {
	.fo_fini          = trigger_fom_fini,
	.fo_tick          = trigger_fom_tick,
	.fo_home_locality = trigger_fom_home_locality,
};

static const struct c2_fom_type_ops trigger_fom_type_ops = {
	.fto_create = trigger_fom_create,
};

static void trigger_rpc_item_reply_cb(struct c2_rpc_item *item)
{
	struct c2_fop *req_fop;
	struct c2_fop *rep_fop;

	C2_PRE(item != NULL);

	req_fop = c2_rpc_item_to_fop(item);

	if (item->ri_error == 0) {
		rep_fop = c2_rpc_item_to_fop(item->ri_reply);
		C2_ASSERT(C2_IN(c2_fop_opcode(rep_fop),
				(C2_SNS_REPAIR_TRIGGER_REP_OPCODE)));
	}
}

static const struct c2_rpc_item_type_ops trigger_item_type_ops = {
	.rito_payload_size   = c2_fop_item_type_default_payload_size,
	.rito_encode         = c2_fop_item_type_default_encode,
	.rito_decode         = c2_fop_item_type_default_decode,
};

const struct c2_rpc_item_ops trigger_fop_rpc_item_ops = {
	.rio_replied = trigger_rpc_item_reply_cb,
	.rio_free    = c2_fop_item_free,
};

void c2_sns_repair_trigger_fop_fini(void)
{
	c2_fop_type_fini(&trigger_fop_fopt);
	c2_fop_type_fini(&trigger_rep_fop_fopt);
	c2_xc_trigger_fop_fini();
}

enum trigger_phases {
	TPH_START = C2_FOPH_NR + 1,
	TPH_WAIT
};

static struct c2_sm_state_descr trigger_phases[] = {
	[TPH_START] = {
		.sd_name      = "Start sns repair",
		.sd_allowed   = C2_BITS(TPH_WAIT)
	},
	[TPH_WAIT] = {
		.sd_name      = "Wait for completion",
		.sd_allowed   = C2_BITS(C2_FOPH_SUCCESS)
	}
};

const struct c2_sm_conf trigger_conf = {
	.scf_name      = "Trigger phases",
	.scf_nr_states = ARRAY_SIZE(trigger_phases),
	.scf_state     = trigger_phases
};

int c2_sns_repair_trigger_fop_init(void)
{
	c2_xc_trigger_fop_init();
	c2_sm_conf_extend(c2_generic_conf.scf_state, trigger_phases,
			  c2_generic_conf.scf_nr_states);
	return  C2_FOP_TYPE_INIT(&trigger_fop_fopt,
			.name      = "sns repair trigger",
			.opcode    = C2_SNS_REPAIR_TRIGGER_OPCODE,
			.xt        = trigger_fop_xc,
			.rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
			C2_RPC_ITEM_TYPE_MUTABO,
			.fom_ops   = &trigger_fom_type_ops,
			.sm        = &trigger_conf,
			.rpc_ops   = &trigger_item_type_ops) ?:
		C2_FOP_TYPE_INIT(&trigger_rep_fop_fopt,
				.name      = "sns repair trigger reply",
				.opcode    = C2_SNS_REPAIR_TRIGGER_REP_OPCODE,
				.xt        = trigger_rep_fop_xc,
				.rpc_flags = C2_RPC_ITEM_TYPE_REPLY,
				.rpc_ops   = &trigger_item_type_ops);

}


static int trigger_fom_create(struct c2_fop *fop, struct c2_fom **out)
{
	struct c2_fom *fom;

	C2_PRE(fop != NULL);
	C2_PRE(out != NULL);

	C2_ALLOC_PTR(fom);
	if (fom == NULL)
		return -ENOMEM;

	c2_fom_init(fom, &fop->f_type->ft_fom_type, &trigger_fom_ops, fop, NULL);

	*out = fom;
	return 0;
}

static void trigger_fom_fini(struct c2_fom *fom)
{
	C2_PRE(fom != NULL);

	c2_fom_fini(fom);
	c2_free(fom);
}

static size_t trigger_fom_home_locality(const struct c2_fom *fom)
{
	C2_PRE(fom != NULL);

	return c2_fop_opcode(fom->fo_fop);
}

static int trigger_fom_tick(struct c2_fom *fom)
{
	int                      rc;
	struct c2_reqh          *reqh;
	struct c2_cm            *cm;
	struct c2_sns_repair_cm     *rcm;
	struct c2_reqh_service      *service;
	struct c2_reqh_service_type *stype;
	struct c2_fop               *rfop;
	struct trigger_fop          *treq;
	struct trigger_rep_fop      *trep;

	if (c2_fom_phase(fom) < C2_FOPH_NR) {
		rc = c2_fom_tick_generic(fom);
	} else {
		reqh = fom->fo_loc->fl_dom->fd_reqh;
		stype = c2_reqh_service_type_find("sns_repair");
		service = c2_reqh_service_find(stype, reqh);
		C2_ASSERT(service != NULL);
		cm = container_of(service, struct c2_cm, cm_service);
		rcm = cm2sns(cm);
		switch(c2_fom_phase(fom)) {
			case TPH_START:
				treq = c2_fop_data(fom->fo_fop);
				rcm->rc_fdata = treq->fdata;
				rcm->rc_file_size = treq->fsize;
				rcm->rc_it.ri_pl.rpl_N = treq->N;
				rcm->rc_it.ri_pl.rpl_K = treq->K;
				rcm->rc_it.ri_pl.rpl_P = treq->P;
				rc = c2_cm_start(cm);
				C2_ASSERT(rc == 0);
				c2_fom_wait_on(fom, &rcm->rc_stop_wait, &fom->fo_cb);
				c2_fom_phase_set(fom, TPH_WAIT);
				rc = C2_FSO_WAIT;
				break;
			case TPH_WAIT:
				rfop = c2_fop_alloc(&trigger_rep_fop_fopt, NULL);
				if (rfop == NULL) {
					c2_fom_phase_set(fom, C2_FOPH_FINISH);
					return C2_FSO_WAIT;
				}
				trep = c2_fop_data(rfop);
				trep->rc = c2_fom_rc(fom);
				fom->fo_rep_fop = rfop;
				c2_cm_stop(&rcm->rc_base);
				c2_fom_phase_set(fom, C2_FOPH_SUCCESS);
				rc = C2_FSO_AGAIN;
				break;
			default:
				C2_ASSERT("Invalid fop" == 0);
		}
	}

	return rc;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
