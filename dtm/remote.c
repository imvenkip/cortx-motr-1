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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 18-Apr-2013
 */


/**
 * @addtogroup dtm
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_DTM

#include "rpc/rpc.h"
#include "rpc/rpc_opcodes.h"  /* M0_DTM_NOTIFICATION_OPCODE */
#include "fop/fop.h"
#include "lib/trace.h"

#include "dtm/dtm_internal.h"
#include "dtm/history.h"
#include "dtm/remote.h"
#include "dtm/remote_xc.h"

enum rem_rpc_notification{
	PERSISTENT = 1,
	FIXED      = 2,
	KNOWN      = 3
};

static const struct m0_dtm_remote_ops rem_rpc_ops;
static void rem_rpc_notify(struct m0_dtm_remote *dtm,
			   const struct m0_dtm_history *history,
			   enum rem_rpc_notification opcode);
static int rem_rpc_item_decode(const struct m0_rpc_item_type *item_type,
			       struct m0_rpc_item **item_out,
			       struct m0_bufvec_cursor *cur);
static struct m0_fop_type rem_rpc_fopt;
static const struct m0_fop_type_ops rem_rpc_ftype_ops;
static struct m0_rpc_item_type_ops rem_rpc_itype_ops;
static const struct m0_rpc_item_ops rem_rpc_item_sender_ops;
static const struct m0_rpc_item_ops rem_rpc_item_receiver_ops;

M0_INTERNAL void m0_dtm_remote_add(struct m0_dtm_remote *dtm,
				   struct m0_dtm_oper *oper,
				   struct m0_dtm_history *history,
				   struct m0_dtm_update *update)
{
	m0_dtm_controlh_add(&dtm->re_fol, oper);
}

M0_INTERNAL void m0_dtm_remote_init(struct m0_dtm_remote *remote,
				    struct m0_dtm *local)
{
	m0_dtm_remote_fol_init(&remote->re_fol, local, remote);
}

M0_INTERNAL void m0_dtm_remote_fini(struct m0_dtm_remote *remote)
{
	m0_dtm_controlh_fini(&remote->re_fol);
}

M0_INTERNAL void m0_dtm_rpc_remote_init(struct m0_dtm_rpc_remote *remote,
					struct m0_dtm *local,
					struct m0_rpc_conn *conn)
{
	m0_dtm_remote_init(&remote->rpr_dtm, local);
	remote->rpr_conn       = conn;
	remote->rpr_dtm.re_ops = &rem_rpc_ops;
}

M0_INTERNAL void m0_dtm_rpc_remote_fini(struct m0_dtm_rpc_remote *remote)
{
	m0_dtm_remote_fini(&remote->rpr_dtm);
}

static void rem_rpc_persistent(struct m0_dtm_remote *dtm,
			       struct m0_dtm_history *history)
{
	rem_rpc_notify(dtm, history, PERSISTENT);
}

static void rem_rpc_fixed(struct m0_dtm_remote *dtm,
			  struct m0_dtm_history *history)
{
	rem_rpc_notify(dtm, history, FIXED);
}

static void rem_rpc_known(struct m0_dtm_remote *dtm,
			  struct m0_dtm_history *history)
{
	rem_rpc_notify(dtm, history, KNOWN);
}

static const struct m0_dtm_remote_ops rem_rpc_ops = {
	.reo_persistent = &rem_rpc_persistent,
	.reo_fixed      = &rem_rpc_fixed,
	.reo_known      = &rem_rpc_known,
};

static void rem_rpc_notify(struct m0_dtm_remote *dtm,
			   const struct m0_dtm_history *history,
			   enum rem_rpc_notification opcode)
{
	struct m0_dtm_rpc_remote *rpr;
	struct m0_fop            *fop;

	M0_PRE(dtm->re_ops == &rem_rpc_ops);
	rpr = container_of(dtm, struct m0_dtm_rpc_remote, rpr_dtm);
	fop = m0_fop_alloc(&rem_rpc_fopt, NULL);
	if (fop != NULL) {
		struct m0_rpc_item   *item;
		struct m0_dtm_notice *notice;

		notice = m0_fop_data(fop);
		notice->dno_opcode = opcode;
		notice->dno_ver    = update_ver(history->h_persistent);
		m0_dtm_history_pack(history, &notice->dno_id);

		item              = &fop->f_item;
		item->ri_prio     = M0_RPC_ITEM_PRIO_MID;
		item->ri_deadline = 0;
		item->ri_ops      = &rem_rpc_item_sender_ops;
		m0_rpc_oneway_item_post(rpr->rpr_conn, item);
	}
}

M0_INTERNAL int m0_dtm_remote_global_init(void)
{
	m0_xc_remote_init();
	rem_rpc_itype_ops = m0_fop_default_item_type_ops;
	rem_rpc_itype_ops.rito_decode = rem_rpc_item_decode;
	return M0_FOP_TYPE_INIT(&rem_rpc_fopt,
				.name      = "dtm notice",
				.opcode    = M0_DTM_NOTIFICATION_OPCODE,
				.xt        = m0_dtm_notice_xc,
				.rpc_flags = M0_RPC_ITEM_TYPE_ONEWAY,
				.fop_ops   = &rem_rpc_ftype_ops,
				.rpc_ops   = &rem_rpc_itype_ops);
}

M0_INTERNAL void m0_dtm_remote_global_fini(void)
{
	m0_fop_type_fini(&rem_rpc_fopt);
	m0_xc_remote_fini();
}

static int rem_rpc_item_decode(const struct m0_rpc_item_type *item_type,
			       struct m0_rpc_item **item_out,
			       struct m0_bufvec_cursor *cur)
{
	int result;

	result = m0_fop_default_item_type_ops.rito_decode(item_type,
							  item_out, cur);
	if (result == 0)
		(*item_out)->ri_ops = &rem_rpc_item_receiver_ops;
	return result;
}

static void rem_rpc_deliver(struct m0_rpc_machine *mach,
			    struct m0_rpc_item *item)
{
	struct m0_dtm_notice  *notice;
	struct m0_dtm_history *history;
	int                    result;

	notice = m0_fop_data(m0_rpc_item_to_fop(item));
	result = m0_dtm_history_unpack(mach->rm_dtm, &notice->dno_id, &history);
	if (result == 0) {
		switch (notice->dno_opcode) {
		case PERSISTENT:
			m0_dtm_history_persistent(history, notice->dno_ver);
			break;
		case FIXED:
		case KNOWN:
			break;
		default:
			M0_LOG(M0_ERROR, "DTM notice: %i.", notice->dno_opcode);
		}
	} else
		M0_LOG(M0_ERROR, "DTM history: %i.", result);
}

static const struct m0_fop_type_ops rem_rpc_ftype_ops = {
	/* nothing */
};

static struct m0_rpc_item_type_ops rem_rpc_itype_ops = {
	/* initialised in m0_dtm_remote_global_init() */
};

static const struct m0_rpc_item_ops rem_rpc_item_sender_ops = {
	/* nothing */
};

static const struct m0_rpc_item_ops rem_rpc_item_receiver_ops = {
	.rio_deliver = &rem_rpc_deliver
};

#undef M0_TRACE_SUBSYSTEM

/** @} end of dtm group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
