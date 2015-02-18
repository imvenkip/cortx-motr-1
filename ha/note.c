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
* Original author: Atsuro Hoshino <atsuro_hoshino@xyratex.com>
* Original creation date: 02-Sep-2013
*/

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_HA

#include "conf/confc.h"
#include "fop/fom_generic.h"
#include "fop/fop.h"
#include "lib/chan.h"
#include "lib/memory.h"
#include "lib/trace.h"
#include "rpc/rpclib.h"
#include "rpc/session.h"

#include "ha/note.h"
#include "ha/note_fops.h"
#include "ha/note_xc.h"

enum TEMPUS {
	/* Timeout for RPC call (seconds) */
	DEADLINE_S             = 0,
	/* Timeout for RPC call (nanoseconds) */
	DEADLINE_NS            = 1000000,
	/* Resend interval for 'state_get' (seconds) */
	RESEND_INTERVAL_GET_S  = 120,
	/* Resend interval for 'state_get' (nanoseconds) */
	RESEND_INTERVAL_GET_NS = 0,
	/* Resend interval for 'state_set' (seconds) */
	RESEND_INTERVAL_SET_S  = 5,
	/* Resend interval for 'state_set' (nanoseconds) */
	RESEND_INTERVAL_SET_NS = 0
};

/**
 * Wrapper structure to enclose m0_fop and pointer to m0_chan.
 *
 * @see struct confc_fop.
 */
struct get_fop_context {
	struct m0_fop   gf_fop;
	struct m0_chan *gf_chan;
};


M0_INTERNAL int m0_ha_state_init(void)
{
	return 0;
}

M0_INTERNAL void m0_ha_state_fini()
{
}

M0_INTERNAL void ha_state_get_replied(struct m0_rpc_item *item)
{
	struct m0_fop          *fop;
	struct m0_ha_state_fop *rep;
	struct get_fop_context *ctx;
	int                     rc;

	M0_ENTRY();
	rc = item->ri_error ?: m0_rpc_item_generic_reply_rc(item->ri_reply);
	fop = m0_rpc_item_to_fop(item->ri_reply);
	M0_AMB(ctx, m0_rpc_item_to_fop(item), gf_fop);
	rep = m0_fop_data(fop);

	if (rc == 0) {
		struct m0_ha_nvec *usr_nvec =
			m0_fop_data(m0_rpc_item_to_fop(item));
		struct m0_ha_nvec *rep_nvec = &rep->hs_note;

		if (rep->hs_rc != 0)
			M0_LOG(M0_NOTICE, "Error: %u.", rep->hs_rc);
		else if (usr_nvec->nv_nr != rep_nvec->nv_nr)
			M0_LOG(M0_NOTICE, "Wrong size: %u != %u.",
			       usr_nvec->nv_nr, rep_nvec->nv_nr);
		else if (m0_forall(i, rep_nvec->nv_nr,
				   m0_fid_eq(&usr_nvec->nv_note[i].no_id,
					     &rep_nvec->nv_note[i].no_id)))
			memcpy(usr_nvec->nv_note, rep_nvec->nv_note,
			       rep_nvec->nv_nr * sizeof rep_nvec->nv_note[0]);
		else
			M0_LOG(M0_NOTICE, "Fid mismatch.");
	}
	m0_chan_signal_lock(ctx->gf_chan);
	m0_fop_put(&ctx->gf_fop);
	M0_LEAVE();
}

const struct m0_rpc_item_ops ha_state_get_ops = {
	.rio_replied = &ha_state_get_replied
};

/**
 * @see: confc_fop_release()
 */
static void get_fop_release(struct m0_ref *ref)
{
	struct m0_fop          *fop;
	struct get_fop_context *ctx;

	M0_ENTRY();
	M0_PRE(ref != NULL);

	M0_AMB(fop, ref, f_ref);
	M0_AMB(ctx, fop, gf_fop);
	/* Clear the fop data field so the user buffer is not released. */
	fop->f_data.fd_data = NULL;
	m0_fop_fini(fop);
	m0_free(ctx);

	M0_LEAVE();
}

M0_INTERNAL int m0_ha_state_get(struct m0_rpc_session *session,
				struct m0_ha_nvec *note, struct m0_chan *chan)
{

	struct m0_rpc_item     *item;
	struct m0_fop          *fop;
	struct get_fop_context *ctx;

	M0_ENTRY();
	M0_PRE(m0_forall(i, note->nv_nr,
			 note->nv_note[i].no_state == M0_NC_UNKNOWN &&
			 m0_conf_fid_is_valid(&note->nv_note[i].no_id)));
	/** @todo once M0_CONF_POOL_TYPE is landed.
 *    ergo(M0_IN(note->nv_note[i].no_state, (M0_NC_REPAIR, M0_NC_REBALANCE)),
 *         m0_conf_fid_type(&note->nv_note[i].no_id) == &M0_CONF_POOL_TYPE))
 */
	M0_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return M0_ERR(-ENOMEM);
	ctx->gf_chan = chan;
	fop = &ctx->gf_fop;
	m0_fop_init(fop, &m0_ha_state_get_fopt, note, get_fop_release);
	item                     = &fop->f_item;
	item->ri_ops             = &ha_state_get_ops;
	item->ri_session         = session;
	item->ri_prio            = M0_RPC_ITEM_PRIO_MID;
	item->ri_deadline        = m0_time_from_now(DEADLINE_S, DEADLINE_NS);
	// wait 2 minutes for the reply
	item->ri_resend_interval = m0_time(RESEND_INTERVAL_GET_S,
					   RESEND_INTERVAL_GET_NS);
	return M0_RC(m0_rpc_post(item));
}

M0_INTERNAL void m0_ha_state_set(struct m0_rpc_session *session,
				 struct m0_ha_nvec *note)
{
	struct m0_fop      *fop;
	struct m0_rpc_item *item;
	int                 rc;

	M0_ENTRY();
	M0_PRE(m0_forall(i, note->nv_nr,
			 note->nv_note[i].no_state != M0_NC_UNKNOWN &&
			 m0_conf_fid_is_valid(&note->nv_note[i].no_id)));
	/** @todo once M0_CONF_POOL_TYPE is landed.
 *    ergo(M0_IN(note->nv_note[i].no_state, (M0_NC_REPAIR, M0_NC_REBALANCE)),
 *         m0_conf_fid_type(&note->nv_note[i].no_id) == &M0_CONF_POOL_TYPE))
 */
	fop = m0_fop_alloc(&m0_ha_state_set_fopt, note,
			   m0_fop_session_machine(session));
	if (fop != NULL) {
		item = &fop->f_item;
		/* wait 5 seconds for the reply */
		item->ri_resend_interval = m0_time(RESEND_INTERVAL_SET_S,
						   RESEND_INTERVAL_SET_NS);

		rc = m0_rpc_post_sync(fop, session, NULL,
				    m0_time_from_now(DEADLINE_S, DEADLINE_NS));
		/* Clear the fop data field so the user buffer is not
		   released. */
		fop->f_data.fd_data = NULL;
		m0_fop_put_lock(fop);
	} else
		M0_LOG(M0_NOTICE, "Cannot allocate fop.");
	M0_LEAVE();
}

M0_INTERNAL void m0_ha_state_accept(struct m0_confc *confc,
				    const struct m0_ha_nvec *note)
{
	struct m0_conf_obj   *obj;
	struct m0_conf_cache *cache;
	struct m0_fid        *id;
	int                   i;

	M0_ENTRY();
	M0_PRE(m0_forall(i, note->nv_nr,
			 note->nv_note[i].no_state != M0_NC_UNKNOWN &&
			 m0_conf_fid_is_valid(&note->nv_note[i].no_id)));
	/** @todo once M0_CONF_POOL_TYPE is landed.
 *    ergo(M0_IN(note->nv_note[i].no_state, (M0_NC_REPAIR, M0_NC_REBALANCE)),
 *         m0_conf_fid_type(&note->nv_note[i].no_id) == &M0_CONF_POOL_TYPE))
 */
	cache = &confc->cc_cache;
	for (i = 0; i < note->nv_nr; ++i) {
		id = &note->nv_note[i].no_id;

		m0_mutex_lock(cache->ca_lock);
		obj = m0_conf_cache_lookup(cache, id);
		if (obj != NULL)
			obj->co_ha_state = note->nv_note[i].no_state;
		m0_mutex_unlock(cache->ca_lock);
	}
	M0_LEAVE();
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
/*
* vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
*/
