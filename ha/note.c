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

M0_EXTERN void m0_ha__session_set(struct m0_rpc_session *session);

M0_INTERNAL int m0_ha_state_init(struct m0_rpc_session *session)
{
	m0_ha__session_set(session);
	return 0;
}

M0_INTERNAL void m0_ha_state_fini()
{
	m0_ha__session_set(NULL);
}

M0_INTERNAL void ha_state_get_replied(struct m0_rpc_item *item)
{
	struct m0_fop          *fop;
	struct m0_ha_state_fop *rep;
	struct get_fop_context *ctx;
	int                     rc;
	struct m0_ha_nvec      *usr_nvec;
	struct m0_ha_nvec      *rep_nvec;

	M0_ENTRY();
	rc = item->ri_error ?: m0_rpc_item_generic_reply_rc(item->ri_reply);
	fop = m0_rpc_item_to_fop(item->ri_reply);
	ctx = M0_AMB(ctx, m0_rpc_item_to_fop(item), gf_fop);
	rep = m0_fop_data(fop);
	usr_nvec = m0_fop_data(m0_rpc_item_to_fop(item));
	rep_nvec = &rep->hs_note;

	if (rc == 0) {
		if (rep->hs_rc != 0)
			rc = M0_ERR(rep->hs_rc);
		else if (usr_nvec->nv_nr != rep_nvec->nv_nr)
			rc = M0_ERR_INFO(-EPROTO, "Wrong size: %u != %u.",
					 usr_nvec->nv_nr, rep_nvec->nv_nr);
		else if (m0_forall(i, rep_nvec->nv_nr,
				   m0_fid_eq(&usr_nvec->nv_note[i].no_id,
					     &rep_nvec->nv_note[i].no_id)))
			memcpy(usr_nvec->nv_note, rep_nvec->nv_note,
			       rep_nvec->nv_nr * sizeof rep_nvec->nv_note[0]);
		else
			rc = M0_ERR_INFO(-EPROTO, "Fid mismatch.");
	}
	if (rc != 0)
		usr_nvec->nv_nr = rc;
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

	fop = M0_AMB(fop, ref, f_ref);
	ctx = M0_AMB(ctx, fop, gf_fop);
	/* Clear the fop data field so the user buffer is not released. */
	fop->f_data.fd_data = NULL;
	m0_fop_fini(fop);
	m0_free(ctx);

	M0_LEAVE();
}

static bool note_invariant(const struct m0_ha_nvec *note, bool known)
{
#define N(i) (note->nv_note[i])
	return m0_forall(i, note->nv_nr,
			 _0C((N(i).no_state != M0_NC_UNKNOWN) == known) &&
			 _0C(!m0_fid_is_set(&N(i).no_id) ||
			     m0_conf_fid_is_valid(&N(i).no_id)) &&
			 _0C(ergo(M0_IN(N(i).no_state,
					(M0_NC_REPAIR, M0_NC_REBALANCE)),
			 m0_conf_fid_type(&N(i).no_id) == &M0_CONF_POOL_TYPE ||
			 m0_conf_fid_type(&N(i).no_id) == &M0_CONF_SDEV_TYPE ||
			 m0_conf_fid_type(&N(i).no_id) == &M0_CONF_DISK_TYPE)));
#undef N
}

M0_INTERNAL int m0_ha_state_get(struct m0_rpc_session *session,
				struct m0_ha_nvec *note, struct m0_chan *chan)
{

	struct m0_rpc_item     *item;
	struct m0_fop          *fop;
	struct get_fop_context *ctx;

	M0_ENTRY("session=%p chan=%p "
		 "note->nv_nr=%"PRIi32" note->nv_note[0].no_id="FID_F
	         " note->nv_note[0].no_state=%u", session, chan, note->nv_nr,
	         FID_P(note->nv_nr > 0 ? &note->nv_note[0].no_id : &M0_FID0),
	         note->nv_nr > 0 ? note->nv_note[0].no_state : 0);
	M0_PRE(note_invariant(note, false));
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
	/* Wait 2 minutes for the reply. */
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

	M0_ENTRY("session=%p note->nv_nr=%"PRIi32" note->nv_note[0].no_id="FID_F
	         " note->nv_note[0].no_state=%u", session, note->nv_nr,
	         FID_P(note->nv_nr > 0 ? &note->nv_note[0].no_id : &M0_FID0),
	         note->nv_nr > 0 ? note->nv_note[0].no_state : 0);
	M0_PRE(note_invariant(note, true));

	fop = m0_fop_alloc(&m0_ha_state_set_fopt, note,
			   m0_fop_session_machine(session));
	if (fop != NULL) {
		item = &fop->f_item;
		/* wait 5 seconds for the reply */
		item->ri_resend_interval = m0_time(RESEND_INTERVAL_SET_S,
						   RESEND_INTERVAL_SET_NS);

		rc = m0_rpc_post_sync(fop, session, NULL,
				    m0_time_from_now(DEADLINE_S, DEADLINE_NS));
		if (rc != 0)
			M0_LOG(M0_NOTICE, "Post failed: %i.", rc);
		/* Clear the fop data field so the user buffer is not
		   released. */
		fop->f_data.fd_data = NULL;
		m0_fop_put_lock(fop);
	} else
		M0_LOG(M0_NOTICE, "Cannot allocate fop.");
	M0_LEAVE();
}

/**
 * Callback used in m0_ha_state_accept(). Updates HA states for particular confc
 * instance during iteration through HA clients list.
 *
 * For internal details see comments provided for m0_ha_state_accept().
 */
static void ha_state_accept(struct m0_confc         *confc,
			    const struct m0_ha_nvec *note)
{
	struct m0_conf_obj   *obj;
	struct m0_conf_cache *cache;
	int                   i;

	M0_ENTRY("confc=%p note->nv_nr=%"PRIi32, confc, note->nv_nr);
	M0_PRE(note_invariant(note, true));

	cache = &confc->cc_cache;
	m0_conf_cache_lock(cache);
	for (i = 0; i < note->nv_nr; ++i) {
		obj = m0_conf_cache_lookup(cache, &note->nv_note[i].no_id);
		M0_LOG(M0_DEBUG, "nv_note[%d]=(no_id="FID_F" no_state=%"PRIu32
		       ") obj=%p obj->co_status=%d", i,
		       FID_P(&note->nv_note[i].no_id), note->nv_note[i].no_state,
		       obj, obj == NULL ? -1 : obj->co_status);
		if (obj != NULL && obj->co_status == M0_CS_READY) {
			obj->co_ha_state = note->nv_note[i].no_state;
			m0_chan_broadcast(&obj->co_ha_chan);
		}
	}
	m0_conf_cache_unlock(cache);
	M0_LEAVE();
}

M0_INTERNAL void m0_ha_state_accept(const struct m0_ha_nvec *note)
{
	m0_ha_clients_iterate((m0_ha_client_cb_t)ha_state_accept, note);
}

M0_INTERNAL int m0_ha_entrypoint_get(struct m0_fop **entrypoint_fop)
{
	struct m0_rpc_session *sess = m0_ha_session_get();
	struct m0_fop         *fop;
	struct m0_rpc_item    *item;
	void                  *data;
	int                    rc;

	M0_ENTRY();
	M0_PRE(entrypoint_fop != NULL);

	if (sess == NULL)
		return M0_ERR(-ENOMEDIUM);
	data = m0_alloc(sizeof (struct m0_ha_entrypoint_req));
	if (data == NULL)
		return M0_ERR(-ENOMEM);
	fop = m0_fop_alloc(&m0_ha_entrypoint_req_fopt, data,
			   m0_fop_session_machine(sess));
	if (fop != NULL) {
		item = &fop->f_item;
		item->ri_resend_interval = m0_time(RESEND_INTERVAL_SET_S,
						   RESEND_INTERVAL_SET_NS);

		rc = m0_rpc_post_sync(fop, sess, NULL,
				    m0_time_from_now(DEADLINE_S, DEADLINE_NS));
		if (rc != 0) {
			m0_free(m0_fop_data(fop));
			fop->f_data.fd_data = NULL;
			m0_fop_put_lock(fop);
		} else {
			*entrypoint_fop = fop;
		}
	} else {
		M0_LOG(M0_NOTICE, "Cannot allocate fop.");
		rc = -ENOMEM;
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
/*
* vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
*/
