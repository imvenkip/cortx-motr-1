/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 7-Apr-2016
 */


/**
 * @addtogroup ha
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_HA
#include "lib/trace.h"

#include "ha/link.h"

#include "lib/memory.h"         /* M0_ALLOC_PTR */
#include "lib/errno.h"          /* ENOMEM */
#include "lib/tlist.h"          /* M0_TL_DESCR_DEFINE */
#include "lib/types.h"          /* m0_uint128 */
#include "lib/misc.h"           /* container_of */

#include "sm/sm.h"              /* m0_sm_state_descr */
#include "rpc/rpc.h"            /* m0_rpc_reply_post */
#include "rpc/rpc_opcodes.h"    /* M0_HA_LINK_OUTGOING_OPCODE */
#include "fop/fom_generic.h"    /* M0_FOPH_FINISH */

#include "ha/link_fops.h"       /* m0_ha_link_msg_fopt */
#include "ha/link_service.h"    /* m0_ha_link_service_register */

/* sent list */
M0_TL_DESCR_DEFINE(ha_sl, "m0_ha_link::hln_sent", static,
		   struct m0_ha_msg_qitem, hmq_link, hmq_magic,
		   5, 6);               /* XXX */
M0_TL_DEFINE(ha_sl, static, struct m0_ha_msg_qitem);

static struct m0_fom_type ha_link_outgoing_fom_type;
extern const struct m0_fom_ops ha_link_outgoing_fom_ops;

M0_INTERNAL int m0_ha_link_init(struct m0_ha_link     *hl,
				struct m0_ha_link_cfg *hl_cfg)
{
	int rc;

	M0_PRE(M0_IS0(hl));

	M0_ENTRY("hl=%p hlc_reqh=%p hlc_reqh_service=%p "
	         "hlc_link_id_local="U128X_F" hlc_link_id_remote="U128X_F,
	         hl, hl_cfg->hlc_reqh, hl_cfg->hlc_reqh_service,
	         U128_P(&hl_cfg->hlc_link_id_local),
	         U128_P(&hl_cfg->hlc_link_id_remote));
	hl->hln_cfg = *hl_cfg;
	ha_sl_tlist_init(&hl->hln_sent);
	m0_mutex_init(&hl->hln_lock);
	m0_mutex_init(&hl->hln_chan_lock);
	m0_chan_init(&hl->hln_chan, &hl->hln_chan_lock);
	m0_ha_msg_queue_init(&hl->hln_q_in, &hl->hln_cfg.hlc_q_in_cfg);
	m0_ha_msg_queue_init(&hl->hln_q_out, &hl->hln_cfg.hlc_q_out_cfg);
	m0_ha_msg_queue_init(&hl->hln_q_delivered,
			     &hl->hln_cfg.hlc_q_delivered_cfg);
	m0_ha_msg_queue_init(&hl->hln_q_not_delivered,
			     &hl->hln_cfg.hlc_q_not_delivered_cfg);
	hl->hln_tag_current = hl->hln_cfg.hlc_tag_even ? 2 : 1;
	m0_fom_init(&hl->hln_fom, &ha_link_outgoing_fom_type,
	            &ha_link_outgoing_fom_ops, NULL, NULL,
	            hl->hln_cfg.hlc_reqh);
	rc = m0_semaphore_init(&hl->hln_stop_cond, 0);
	M0_ASSERT(rc == 0);
	rc = m0_semaphore_init(&hl->hln_stop_wait, 0);
	M0_ASSERT(rc == 0);
	hl->hln_waking_up = false;
	hl->hln_fom_is_stopping = false;
	return M0_RC(0);
}

M0_INTERNAL void m0_ha_link_fini(struct m0_ha_link *hl)
{
	M0_ENTRY("hl=%p", hl);
	m0_semaphore_fini(&hl->hln_stop_wait);
	m0_semaphore_fini(&hl->hln_stop_cond);
	m0_ha_msg_queue_fini(&hl->hln_q_not_delivered);
	m0_ha_msg_queue_fini(&hl->hln_q_delivered);
	m0_ha_msg_queue_fini(&hl->hln_q_out);
	m0_ha_msg_queue_fini(&hl->hln_q_in);
	m0_chan_fini_lock(&hl->hln_chan);
	m0_mutex_fini(&hl->hln_chan_lock);
	m0_mutex_fini(&hl->hln_lock);
	ha_sl_tlist_fini(&hl->hln_sent);
	M0_LEAVE();
}

M0_INTERNAL void m0_ha_link_start(struct m0_ha_link *hl)
{
	M0_ENTRY("hl=%p", hl);
	m0_ha_link_service_register(hl->hln_cfg.hlc_reqh_service, hl);
	m0_fom_queue(&hl->hln_fom);
	hl->hln_fom_locality = &hl->hln_fom.fo_loc->fl_locality;
	M0_LEAVE();
}

static void ha_link_outgoing_fom_wakeup(struct m0_ha_link *hl);

M0_INTERNAL void m0_ha_link_stop(struct m0_ha_link *hl)
{
	M0_ENTRY("hl=%p", hl);
	m0_semaphore_up(&hl->hln_stop_cond);
	ha_link_outgoing_fom_wakeup(hl);
	m0_semaphore_down(&hl->hln_stop_wait);
	m0_ha_link_service_deregister(hl->hln_cfg.hlc_reqh_service, hl);
	M0_LEAVE();
}

M0_INTERNAL struct m0_chan *m0_ha_link_chan(struct m0_ha_link *hl)
{
	return &hl->hln_chan;
}

enum ha_link_send_type {
	HA_LINK_SEND_QUERY,
	HA_LINK_SEND_POST,
	HA_LINK_SEND_REPLY,
};

M0_INTERNAL void m0_ha_link_send(struct m0_ha_link      *hl,
                                 const struct m0_ha_msg *msg,
                                 uint64_t               *tag)
{
	struct m0_ha_msg_qitem *qitem;

	M0_ENTRY("hl=%p msg=%p", hl, msg);
	m0_mutex_lock(&hl->hln_lock);
	qitem = m0_ha_msg_queue_alloc(&hl->hln_q_out);
	M0_ASSERT(qitem != NULL);       /* XXX */
	qitem->hmq_msg = *msg;
	qitem->hmq_msg.hm_tag = hl->hln_tag_current;
	qitem->hmq_msg.hm_link_id = hl->hln_cfg.hlc_link_id_remote;
	hl->hln_tag_current += 2;
	*tag = qitem->hmq_msg.hm_tag;
	qitem->hmq_msg.hm_incoming = false;
	m0_ha_msg_queue_enqueue(&hl->hln_q_out, qitem);
	m0_mutex_unlock(&hl->hln_lock);
	ha_link_outgoing_fom_wakeup(hl);
	M0_LEAVE("hl=%p msg=%p tag=%"PRIu64, hl, msg, *tag);
}

M0_INTERNAL struct m0_ha_msg *m0_ha_link_recv(struct m0_ha_link *hl,
					      uint64_t          *tag)
{
	struct m0_ha_msg_qitem *qitem;

	m0_mutex_lock(&hl->hln_lock);
	qitem = m0_ha_msg_queue_dequeue(&hl->hln_q_in);
	if (qitem != NULL)
		*tag = qitem->hmq_msg.hm_tag;
	m0_mutex_unlock(&hl->hln_lock);

	M0_LOG(M0_DEBUG, "hl=%p qitem=%p msg=%p tag=%"PRIu64,
	       hl, qitem, qitem == NULL ? NULL : &qitem->hmq_msg,
	       qitem == NULL ? M0_HA_MSG_TAG_UNKNOWN : *tag);
	return qitem == NULL ? NULL : &qitem->hmq_msg;
}

M0_INTERNAL void m0_ha_link_delivered(struct m0_ha_link *hl,
				      struct m0_ha_msg  *msg)
{
	struct m0_ha_msg_qitem *qitem;
	struct m0_ha_msg_queue *mq;

	qitem = container_of(msg, struct m0_ha_msg_qitem, hmq_msg);
	M0_LOG(M0_DEBUG, "msg=%p qitem=%p tag=%"PRIu64,
	       msg, qitem, msg->hm_tag);

	m0_mutex_lock(&hl->hln_lock);
	mq = msg->hm_incoming ? &hl->hln_q_in : &hl->hln_q_out;
	m0_ha_msg_queue_free(mq, qitem);
	m0_mutex_unlock(&hl->hln_lock);
}

M0_INTERNAL bool m0_ha_link_msg_is_delivered(struct m0_ha_link *hl,
					     uint64_t           tag)
{
	bool delivered;

	m0_mutex_lock(&hl->hln_lock);
	delivered = m0_ha_msg_queue_find(&hl->hln_q_out, tag) == NULL &&
		    m0_tl_find(ha_sl, qitem, &hl->hln_sent,
		               qitem->hmq_msg.hm_tag == tag) == NULL;
	m0_mutex_unlock(&hl->hln_lock);
	return delivered;
}

static uint64_t ha_link_q_consume(struct m0_ha_link      *hl,
                                  struct m0_ha_msg_queue *mq)
{
	struct m0_ha_msg_qitem *qitem;
	uint64_t                tag;

	M0_PRE(M0_IN(mq, (&hl->hln_q_delivered, &hl->hln_q_not_delivered)));
	m0_mutex_lock(&hl->hln_lock);
	qitem = m0_ha_msg_queue_dequeue(&hl->hln_q_delivered);
	if (qitem != NULL) {
		tag = qitem->hmq_msg.hm_tag;
		m0_ha_msg_queue_free(&hl->hln_q_delivered, qitem);
	} else {
		tag = M0_HA_MSG_TAG_INVALID;
	}
	m0_mutex_unlock(&hl->hln_lock);

	M0_LOG(M0_DEBUG, "hl=%p mq=%p tag=%"PRIu64, hl, mq, tag);
	return tag;
}

M0_INTERNAL uint64_t m0_ha_link_delivered_consume(struct m0_ha_link *hl)
{
	return ha_link_q_consume(hl, &hl->hln_q_delivered);
}

M0_INTERNAL uint64_t m0_ha_link_not_delivered_consume(struct m0_ha_link *hl)
{
	return ha_link_q_consume(hl, &hl->hln_q_not_delivered);
}

struct ha_link_wait_ctx {
	struct m0_ha_link  *hwc_hl;
	uint64_t            hwc_tag;
	struct m0_clink     hwc_clink;
	struct m0_semaphore hwc_sem;
	bool                hwc_check_disable;
};

static void ha_link_wait(struct ha_link_wait_ctx *wait_ctx,
					  bool (*check)(struct m0_clink *clink))
{
	int rc;

	m0_clink_init(&wait_ctx->hwc_clink, check);
	rc = m0_semaphore_init(&wait_ctx->hwc_sem, 0);
	M0_ASSERT(rc == 0);     /* XXX */
	m0_clink_add_lock(m0_ha_link_chan(wait_ctx->hwc_hl),
			  &wait_ctx->hwc_clink);
	check(&wait_ctx->hwc_clink);
	m0_semaphore_down(&wait_ctx->hwc_sem);
	m0_clink_del_lock(&wait_ctx->hwc_clink);
	m0_semaphore_fini(&wait_ctx->hwc_sem);
	m0_clink_fini(&wait_ctx->hwc_clink);
}

static bool ha_link_wait_delivery_check(struct m0_clink *clink)
{
	struct ha_link_wait_ctx *wait_ctx;

	/* XXX bob_of */
	wait_ctx = container_of(clink, struct ha_link_wait_ctx, hwc_clink);
	if (!wait_ctx->hwc_check_disable &&
	    m0_ha_link_msg_is_delivered(wait_ctx->hwc_hl, wait_ctx->hwc_tag)) {
		wait_ctx->hwc_check_disable = true;
		m0_semaphore_up(&wait_ctx->hwc_sem);
	}
	return false;
}

M0_INTERNAL void m0_ha_link_wait_delivery(struct m0_ha_link *hl, uint64_t tag)
{
	bool                    delivered;
	struct ha_link_wait_ctx wait_ctx = {
		.hwc_hl            = hl,
		.hwc_tag           = tag,
		.hwc_check_disable = false,
	};

	M0_ENTRY("hl=%p tag=%"PRIu64, hl, tag);
	ha_link_wait(&wait_ctx, &ha_link_wait_delivery_check);
	delivered = m0_ha_link_msg_is_delivered(hl, tag);
	M0_ASSERT(delivered);
	M0_LEAVE("hl=%p tag=%"PRIu64, hl, tag);
}

static bool ha_link_wait_arrival_check(struct m0_clink *clink)
{
	struct ha_link_wait_ctx *wait_ctx;
	bool                     arrived;
	struct m0_ha_link       *hl;

	/* XXX bob_of */
	wait_ctx = container_of(clink, struct ha_link_wait_ctx, hwc_clink);
	hl = wait_ctx->hwc_hl;
	M0_ENTRY("hl=%p", hl);
	if (!wait_ctx->hwc_check_disable) {
		m0_mutex_lock(&hl->hln_lock);
		arrived = !m0_ha_msg_queue_is_empty(&hl->hln_q_in);
		m0_mutex_unlock(&hl->hln_lock);
		if (arrived) {
			wait_ctx->hwc_check_disable = true;
			m0_semaphore_up(&wait_ctx->hwc_sem);
		}
		M0_LOG(M0_DEBUG, "hl=%p arrived=%d", hl, !!arrived);
	}
	M0_LEAVE("hl=%p", hl);
	return false;
}

M0_INTERNAL void m0_ha_link_wait_arrival(struct m0_ha_link *hl)
{
	bool                    arrived;
	struct ha_link_wait_ctx wait_ctx = {
		.hwc_hl            = hl,
		.hwc_check_disable = false,
	};

	M0_ENTRY("hl=%p", hl);
	ha_link_wait(&wait_ctx, &ha_link_wait_arrival_check);
	m0_mutex_lock(&hl->hln_lock);
	arrived = !m0_ha_msg_queue_is_empty(&hl->hln_q_in);
	m0_mutex_unlock(&hl->hln_lock);
	M0_ASSERT(arrived);
	M0_LEAVE("hl=%p", hl);
}

M0_INTERNAL void m0_ha_link_flush(struct m0_ha_link *hl)
{
}

static int ha_link_incoming_fom_tick(struct m0_fom *fom)
{
	struct m0_ha_link_msg_fop     *req_fop;
	struct m0_ha_link_msg_rep_fop *rep_fop;
	struct m0_ha_msg_qitem        *qitem;
	struct m0_ha_msg              *msg;
	struct m0_ha_link             *hl;

	req_fop = m0_fop_data(fom->fo_fop);
	rep_fop = m0_fop_data(fom->fo_rep_fop);

	hl = m0_ha_link_service_find(fom->fo_service,
	                             &req_fop->lmf_msg.hm_link_id);
	M0_LOG(M0_DEBUG, "fom=%p hl=%p", fom, hl);
	if (hl == NULL) {
		msg = &req_fop->lmf_msg;
		M0_LOG(M0_WARN, "ep=%s hm_link_id="U128X_F" hm_fid="FID_F" "
		       "hm_tag=%"PRIu64" hed_type=%"PRIu64,
		       m0_rpc_conn_addr(m0_fop_to_rpc_item(fom->fo_fop)->
							ri_session->s_conn),
		       U128_P(&msg->hm_link_id), FID_P(&msg->hm_fid),
		       msg->hm_tag, msg->hm_data.hed_type);
		rep_fop->lmr_rc = -EBADSLT;
	} else {
		m0_mutex_lock(&hl->hln_lock);
		qitem = m0_ha_msg_queue_alloc(&hl->hln_q_in);
		M0_ASSERT(qitem != NULL);       /* XXX */
		qitem->hmq_msg = req_fop->lmf_msg;
		qitem->hmq_msg.hm_incoming = true;
		m0_ha_msg_queue_enqueue(&hl->hln_q_in, qitem);
		m0_mutex_unlock(&hl->hln_lock);
		m0_chan_broadcast_lock(&hl->hln_chan);
		rep_fop->lmr_rc = 0;
		M0_LOG(M0_DEBUG, "hl=%p qitem=%p tag=%"PRIu64" type=%"PRIu64" "
		       "lmr_rc=%d", hl, qitem, req_fop->lmf_msg.hm_tag,
		       req_fop->lmf_msg.hm_data.hed_type, rep_fop->lmr_rc);
	}

        m0_rpc_reply_post(m0_fop_to_rpc_item(fom->fo_fop),
			  m0_fop_to_rpc_item(fom->fo_rep_fop));
        m0_fom_phase_set(fom, M0_FOPH_FINISH);
        return M0_FSO_WAIT;
}

static void ha_link_incoming_fom_fini(struct m0_fom *fom)
{
	m0_fom_fini(fom);
	m0_free(fom);
}

static size_t ha_link_incoming_fom_locality(const struct m0_fom *fom)
{
	return 0;
}

const struct m0_fom_ops ha_link_incoming_fom_ops = {
	.fo_tick          = &ha_link_incoming_fom_tick,
	.fo_fini          = &ha_link_incoming_fom_fini,
	.fo_home_locality = &ha_link_incoming_fom_locality,
};

static int ha_link_incoming_fom_create(struct m0_fop   *fop,
                                       struct m0_fom  **m,
                                       struct m0_reqh  *reqh)
{
	struct m0_fom                 *fom;
	struct m0_ha_link_msg_rep_fop *reply;

	M0_PRE(fop != NULL);
	M0_PRE(m != NULL);

	M0_ALLOC_PTR(fom);
	if (fom == NULL)
		return M0_ERR(-ENOMEM);

	M0_ALLOC_PTR(reply);
	if (reply == NULL) {
		m0_free(fom);
		return M0_ERR(-ENOMEM);
	}

	fom->fo_rep_fop = m0_fop_alloc(&m0_ha_link_msg_rep_fopt,
				       reply, m0_fop_rpc_machine(fop));
	if (fom->fo_rep_fop == NULL) {
		m0_free(reply);
		m0_free(fom);
		return M0_ERR(-ENOMEM);
	}

	m0_fom_init(fom, &fop->f_type->ft_fom_type, &ha_link_incoming_fom_ops,
		    fop, fom->fo_rep_fop, reqh);

	*m = fom;
	return M0_RC(0);
}

const struct m0_fom_type_ops m0_ha_link_incoming_fom_type_ops = {
	.fto_create = &ha_link_incoming_fom_create,
};

enum ha_link_outgoing_fom_state {
	HA_LINK_OUTGOING_STATE_INIT   = M0_FOM_PHASE_INIT,
	HA_LINK_OUTGOING_STATE_FINISH = M0_FOM_PHASE_FINISH,
	HA_LINK_OUTGOING_STATE_IDLE,
	HA_LINK_OUTGOING_STATE_SEND,
	HA_LINK_OUTGOING_STATE_WAIT_REPLY,
	HA_LINK_OUTGOING_STATE_NR,
};

static struct m0_sm_state_descr
ha_link_outgoing_fom_states[HA_LINK_OUTGOING_STATE_NR] = {
#define _ST(name, flags, allowed)      \
	[name] = {                    \
		.sd_flags   = flags,  \
		.sd_name    = #name,  \
		.sd_allowed = allowed \
	}
	_ST(HA_LINK_OUTGOING_STATE_INIT, M0_SDF_INITIAL,
	   M0_BITS(HA_LINK_OUTGOING_STATE_IDLE)),
	_ST(HA_LINK_OUTGOING_STATE_IDLE, 0,
	   M0_BITS(HA_LINK_OUTGOING_STATE_SEND,
	           HA_LINK_OUTGOING_STATE_FINISH)),
	_ST(HA_LINK_OUTGOING_STATE_SEND, 0,
	   M0_BITS(HA_LINK_OUTGOING_STATE_WAIT_REPLY)),
	_ST(HA_LINK_OUTGOING_STATE_WAIT_REPLY, 0,
	   M0_BITS(HA_LINK_OUTGOING_STATE_IDLE)),
	_ST(HA_LINK_OUTGOING_STATE_FINISH, M0_SDF_TERMINAL, 0),
#undef _ST
};

const static struct m0_sm_conf ha_link_outgoing_fom_conf = {
	.scf_name      = "ha_link_outgoing_fom",
	.scf_nr_states = ARRAY_SIZE(ha_link_outgoing_fom_states),
	.scf_state     = ha_link_outgoing_fom_states,
};

static void ha_link_outgoing_item_sent(struct m0_rpc_item *item)
{
	struct m0_ha_link *hl;

	/* XXX bob_of */
	hl = container_of(container_of(item, struct m0_fop, f_item),
	                  struct m0_ha_link, hln_outgoing_fop);
	M0_ENTRY("hl=%p item=%p", hl, item);
	M0_LEAVE();
}

static void ha_link_outgoing_item_replied(struct m0_rpc_item *item)
{
	struct m0_ha_link *hl;

	/* XXX bob_of */
	hl = container_of(container_of(item, struct m0_fop, f_item),
	                  struct m0_ha_link, hln_outgoing_fop);
	M0_ENTRY("hl=%p item=%p", hl, item);
	m0_mutex_lock(&hl->hln_lock);
	hl->hln_replied = true;
	m0_mutex_unlock(&hl->hln_lock);
	ha_link_outgoing_fom_wakeup(hl);
	M0_LEAVE();
}

static void ha_link_outgoing_fop_release(struct m0_ref *ref)
{
	struct m0_ha_link *hl;
	struct m0_fop     *fop = container_of(ref, struct m0_fop, f_ref);

	/* XXX bob_of */
	hl = container_of(fop, struct m0_ha_link, hln_outgoing_fop);
	M0_ENTRY("hl=%p fop=%p", hl, fop);
	fop->f_data.fd_data = NULL;
	m0_fop_fini(fop);
	M0_LEAVE();
}

const static struct m0_rpc_item_ops ha_link_outgoing_item_ops = {
	.rio_sent    = &ha_link_outgoing_item_sent,
	.rio_replied = &ha_link_outgoing_item_replied,
};

static int ha_link_outgoing_fom_tick(struct m0_fom *fom)
{
	enum ha_link_outgoing_fom_state  phase;
	struct m0_ha_msg_qitem          *qitem;
	struct m0_ha_msg_qitem          *qitem2;
	struct m0_ha_link               *hl;
	struct m0_rpc_item              *item;
	bool                             replied;

	hl = container_of(fom, struct m0_ha_link, hln_fom); /* XXX bob_of */
	phase = m0_fom_phase(&hl->hln_fom);
	M0_ENTRY("hl=%p phase=%s", hl, m0_fom_phase_name(&hl->hln_fom, phase));

	switch (phase) {
	case HA_LINK_OUTGOING_STATE_INIT:
		hl->hln_qitem_to_send = NULL;
		m0_fom_phase_set(fom, HA_LINK_OUTGOING_STATE_IDLE);
		return M0_RC(M0_FSO_AGAIN);
	case HA_LINK_OUTGOING_STATE_IDLE:
		M0_ASSERT(hl->hln_qitem_to_send == NULL);
		hl->hln_replied = false;
		if (m0_semaphore_trydown(&hl->hln_stop_cond)) {
			m0_mutex_lock(&hl->hln_lock);
			hl->hln_fom_is_stopping = true;
			while ((qitem = m0_ha_msg_queue_dequeue(&hl->hln_q_out))
			       != NULL) {
				qitem2 = m0_ha_msg_queue_alloc(
				                &hl->hln_q_not_delivered);
				/*
				 * TODO may be optimised, see the similar
				 * assignment below
				 */
				qitem2->hmq_msg = qitem->hmq_msg;
				m0_ha_msg_queue_free(&hl->hln_q_out, qitem);
				m0_ha_msg_queue_enqueue(
				        &hl->hln_q_not_delivered, qitem2);
			}
			m0_mutex_unlock(&hl->hln_lock);
			m0_chan_broadcast_lock(&hl->hln_chan);
			m0_sm_ast_cancel(hl->hln_fom_locality->lo_grp,
			                 &hl->hln_waking_ast);
			m0_fom_phase_set(fom, HA_LINK_OUTGOING_STATE_FINISH);
			return M0_RC(M0_FSO_WAIT);
		}
		m0_mutex_lock(&hl->hln_lock);
		hl->hln_qitem_to_send = m0_ha_msg_queue_dequeue(&hl->hln_q_out);
		if (hl->hln_qitem_to_send != NULL)
			ha_sl_tlink_init_at_tail(hl->hln_qitem_to_send,
						 &hl->hln_sent);
		m0_mutex_unlock(&hl->hln_lock);
		if (hl->hln_qitem_to_send != NULL) {
			m0_fom_phase_set(fom, HA_LINK_OUTGOING_STATE_SEND);
			return M0_RC(M0_FSO_AGAIN);
		}
		return M0_RC(M0_FSO_WAIT);
	case HA_LINK_OUTGOING_STATE_SEND:
		M0_SET0(&hl->hln_outgoing_fop);
		M0_ALLOC_PTR(hl->hln_req_fop_data);
		M0_ASSERT(hl->hln_req_fop_data != NULL);
		m0_fop_init(&hl->hln_outgoing_fop, &m0_ha_link_msg_fopt,
		            hl->hln_req_fop_data,
			    &ha_link_outgoing_fop_release);
		hl->hln_req_fop_data->lmf_msg = hl->hln_qitem_to_send->hmq_msg;
		item = m0_fop_to_rpc_item(&hl->hln_outgoing_fop);
		item->ri_prio = M0_RPC_ITEM_PRIO_MID;
		item->ri_deadline = m0_time_from_now(0, 0);
		item->ri_ops = &ha_link_outgoing_item_ops;
		item->ri_session = hl->hln_cfg.hlc_rpc_session;
		m0_rpc_post(item);
		m0_fom_phase_set(fom, HA_LINK_OUTGOING_STATE_WAIT_REPLY);
		return M0_RC(M0_FSO_AGAIN);
	case HA_LINK_OUTGOING_STATE_WAIT_REPLY:
		m0_mutex_lock(&hl->hln_lock);
		replied = hl->hln_replied;
		m0_mutex_unlock(&hl->hln_lock);
		if (replied) {
			m0_fop_put_lock(&hl->hln_outgoing_fop);
			m0_free(hl->hln_req_fop_data);
			m0_mutex_lock(&hl->hln_lock);
			ha_sl_tlink_del_fini(hl->hln_qitem_to_send);
			qitem = m0_ha_msg_queue_alloc(&hl->hln_q_delivered);
			M0_ASSERT(qitem != NULL);       /* XXX */
			/*
			 * TODO optimize it by only keeping message tags in the
			 * queue. It may be a performance optimisation but it
			 * may complicate the debugging.
			 */
			qitem->hmq_msg = hl->hln_qitem_to_send->hmq_msg;
			m0_ha_msg_queue_free(&hl->hln_q_out,
			                     hl->hln_qitem_to_send);
			m0_ha_msg_queue_enqueue(&hl->hln_q_delivered, qitem);
			m0_mutex_unlock(&hl->hln_lock);
			m0_chan_broadcast_lock(&hl->hln_chan);
			hl->hln_qitem_to_send = NULL;
			m0_fom_phase_set(fom, HA_LINK_OUTGOING_STATE_IDLE);
			return M0_RC(M0_FSO_AGAIN);
		}
		return M0_FSO_WAIT;
	case HA_LINK_OUTGOING_STATE_FINISH:
	case HA_LINK_OUTGOING_STATE_NR:
		M0_IMPOSSIBLE("");
	}
        return M0_RC(M0_FSO_WAIT);
}

static void ha_link_outgoing_fom_wakeup_ast(struct m0_sm_group *gr,
                                            struct m0_sm_ast   *ast)
{
	struct m0_ha_link *hl = ast->sa_datum;

	M0_ENTRY("hl=%p", hl);
	m0_mutex_lock(&hl->hln_lock);
	hl->hln_waking_up = false;
	m0_mutex_unlock(&hl->hln_lock);
	if (m0_fom_is_waiting(&hl->hln_fom)) {
		M0_LOG(M0_DEBUG, "waking up");
		m0_fom_ready(&hl->hln_fom);
	}
	M0_LEAVE();
}

static void ha_link_outgoing_fom_wakeup(struct m0_ha_link *hl)
{
	M0_ENTRY("hl=%p", hl);
	m0_mutex_lock(&hl->hln_lock);
	if (!hl->hln_waking_up && !hl->hln_fom_is_stopping) {
		M0_LOG(M0_DEBUG, "posting ast");
		hl->hln_waking_up = true;
		hl->hln_waking_ast = (struct m0_sm_ast){
			.sa_cb    = &ha_link_outgoing_fom_wakeup_ast,
			.sa_datum = hl,
		};
		m0_sm_ast_post(hl->hln_fom_locality->lo_grp,
			       &hl->hln_waking_ast);
	}
	m0_mutex_unlock(&hl->hln_lock);
	M0_LEAVE();
}

static void ha_link_outgoing_fom_fini(struct m0_fom *fom)
{
	struct m0_ha_link *hl;

	hl = container_of(fom, struct m0_ha_link, hln_fom); /* XXX bob_of */
	M0_ENTRY("fom=%p hl=%p", fom, hl);
	m0_fom_fini(fom);
	m0_semaphore_up(&hl->hln_stop_wait);
	M0_LEAVE();
}

static size_t ha_link_outgoing_fom_locality(const struct m0_fom *fom)
{
	return 0;
}

const struct m0_fom_ops ha_link_outgoing_fom_ops = {
	.fo_tick          = &ha_link_outgoing_fom_tick,
	.fo_fini          = &ha_link_outgoing_fom_fini,
	.fo_home_locality = &ha_link_outgoing_fom_locality,
};

static int ha_link_outgoing_fom_create(struct m0_fop   *fop,
                                       struct m0_fom  **m,
                                       struct m0_reqh  *reqh)
{
	struct m0_fom *fom;

	M0_PRE(fop != NULL);
	M0_PRE(m != NULL);

	M0_ALLOC_PTR(fom);
	if (fom == NULL)
		return M0_ERR(-ENOMEM);

	m0_fom_init(fom, &fop->f_type->ft_fom_type, &ha_link_outgoing_fom_ops,
		    fop, fom->fo_rep_fop, reqh);

	*m = fom;
	return M0_RC(0);
}

const struct m0_fom_type_ops m0_ha_link_outgoing_fom_type_ops = {
	.fto_create = &ha_link_outgoing_fom_create,
};

M0_INTERNAL int m0_ha_link_mod_init(void)
{
	int rc;

	rc = m0_ha_link_fops_init();
	M0_ASSERT(rc == 0);
	m0_fom_type_init(&ha_link_outgoing_fom_type, M0_HA_LINK_OUTGOING_OPCODE,
			 &m0_ha_link_outgoing_fom_type_ops,
			 &m0_ha_link_service_type, &ha_link_outgoing_fom_conf);
	return 0;
}

M0_INTERNAL void m0_ha_link_mod_fini(void)
{
	m0_ha_link_fops_fini();
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of ha group */

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
