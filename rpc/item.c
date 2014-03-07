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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 *		    Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 06/27/2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/tlist.h"
#include "lib/rwlock.h"
#include "lib/misc.h"
#include "lib/errno.h"
#include "lib/finject.h"
#include "ha/epoch.h"
#include "mero/magic.h"
#include "rpc/rpc_internal.h"

/**
   @addtogroup rpc

   @{
 */

static int item_entered_in_urgent_state(struct m0_sm *mach);
static void item_timer_cb(struct m0_sm_timer *timer);
static void item_timedout(struct m0_rpc_item *item);
static void item_resend(struct m0_rpc_item *item);
static int item_reply_received(struct m0_rpc_item *reply,
			       struct m0_rpc_item **req_out);
static int req_replied(struct m0_rpc_item *req, struct m0_rpc_item *reply);

M0_TL_DESCR_DEFINE(rpcitem, "rpc item tlist", M0_INTERNAL,
		   struct m0_rpc_item, ri_field,
	           ri_magic, M0_RPC_ITEM_MAGIC,
		   M0_RPC_ITEM_HEAD_MAGIC);

M0_TL_DEFINE(rpcitem, M0_INTERNAL, struct m0_rpc_item);

M0_TL_DESCR_DEFINE(rit, "rpc_item_type_descr", static, struct m0_rpc_item_type,
		   rit_linkage,	rit_magic, M0_RPC_ITEM_TYPE_MAGIC,
		   M0_RPC_ITEM_TYPE_HEAD_MAGIC);

M0_TL_DEFINE(rit, static, struct m0_rpc_item_type);

/** Global rpc item types list. */
static struct m0_tl        rpc_item_types_list;
static struct m0_rwlock    rpc_item_types_lock;

/**
  Checks if the supplied opcode has already been registered.
  @param opcode RPC item type opcode.
  @retval true if opcode is a duplicate(already registered)
  @retval false if opcode has not been registered yet.
*/
static bool opcode_is_dup(uint32_t opcode)
{
	M0_PRE(opcode > 0);

	return m0_rpc_item_type_lookup(opcode) != NULL;
}

M0_INTERNAL m0_bcount_t m0_rpc_item_onwire_header_size;

#define HEADER1_XCODE_OBJ(ptr) M0_XCODE_OBJ(m0_rpc_item_header1_xc, ptr)
#define HEADER2_XCODE_OBJ(ptr) M0_XCODE_OBJ(m0_rpc_item_header2_xc, ptr)

M0_INTERNAL int m0_rpc_item_module_init(void)
{
	struct m0_rpc_item_header1 h1;
	struct m0_xcode_ctx        h1_xc;
	struct m0_rpc_item_header2 h2;
	struct m0_xcode_ctx        h2_xc;

	M0_ENTRY();

	/**
	 * @todo This should be done from dtm subsystem init.
	 */
	m0_xc_rpc_onwire_init();
	m0_xc_cookie_init();

	m0_rwlock_init(&rpc_item_types_lock);
	rit_tlist_init(&rpc_item_types_list);

	m0_xcode_ctx_init(&h1_xc, &HEADER1_XCODE_OBJ(&h1));
	m0_xcode_ctx_init(&h2_xc, &HEADER2_XCODE_OBJ(&h2));
	m0_rpc_item_onwire_header_size = m0_xcode_length(&h1_xc) +
		m0_xcode_length(&h2_xc);

	M0_RETURN(0);
}

M0_INTERNAL void m0_rpc_item_module_fini(void)
{
	struct m0_rpc_item_type		*item_type;

	M0_ENTRY();

	m0_rwlock_write_lock(&rpc_item_types_lock);
	m0_tl_for(rit, &rpc_item_types_list, item_type) {
		rit_tlink_del_fini(item_type);
	} m0_tl_endfor;
	rit_tlist_fini(&rpc_item_types_list);
	m0_rwlock_write_unlock(&rpc_item_types_lock);
	m0_rwlock_fini(&rpc_item_types_lock);

	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_item_type_register(struct m0_rpc_item_type *item_type)
{
	uint64_t dir_flag;
	M0_ENTRY("item_type: %p, item_opcode: %u", item_type,
		 item_type->rit_opcode);
	M0_PRE(item_type != NULL);
	dir_flag = item_type->rit_flags & (M0_RPC_ITEM_TYPE_REQUEST |
		   M0_RPC_ITEM_TYPE_REPLY | M0_RPC_ITEM_TYPE_ONEWAY);
	M0_PRE(!opcode_is_dup(item_type->rit_opcode));
	M0_PRE(m0_is_po2(dir_flag));
	M0_PRE(ergo(item_type->rit_flags & M0_RPC_ITEM_TYPE_MUTABO,
		    dir_flag == M0_RPC_ITEM_TYPE_REQUEST));

	m0_rwlock_write_lock(&rpc_item_types_lock);
	rit_tlink_init_at(item_type, &rpc_item_types_list);
	m0_rwlock_write_unlock(&rpc_item_types_lock);

	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_item_type_deregister(struct m0_rpc_item_type *item_type)
{
	M0_ENTRY("item_type: %p", item_type);
	M0_PRE(item_type != NULL);

	m0_rwlock_write_lock(&rpc_item_types_lock);
	rit_tlink_del_fini(item_type);
	item_type->rit_magic = 0;
	m0_rwlock_write_unlock(&rpc_item_types_lock);

	M0_LEAVE();
}

M0_INTERNAL struct m0_rpc_item_type *m0_rpc_item_type_lookup(uint32_t opcode)
{
	struct m0_rpc_item_type *item_type;

	M0_ENTRY("opcode: %u", opcode);

	m0_rwlock_read_lock(&rpc_item_types_lock);
	m0_tl_for(rit, &rpc_item_types_list, item_type) {
		if (item_type->rit_opcode == opcode) {
			break;
		}
	} m0_tl_endfor;
	m0_rwlock_read_unlock(&rpc_item_types_lock);

	M0_POST(ergo(item_type != NULL, item_type->rit_opcode == opcode));
	M0_LEAVE("item_type: %p", item_type);
	return item_type;
}

static struct m0_sm_state_descr outgoing_item_states[] = {
	[M0_RPC_ITEM_UNINITIALISED] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "UNINITIALISED",
		.sd_allowed = 0,
	},
	[M0_RPC_ITEM_INITIALISED] = {
		.sd_flags   = M0_SDF_INITIAL | M0_SDF_FINAL,
		.sd_name    = "INITIALISED",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_ENQUEUED,
				      M0_RPC_ITEM_URGENT,
				      M0_RPC_ITEM_SENDING,
				      M0_RPC_ITEM_FAILED,
				      M0_RPC_ITEM_UNINITIALISED),
	},
	[M0_RPC_ITEM_ENQUEUED] = {
		.sd_name    = "ENQUEUED",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_SENDING,
				      M0_RPC_ITEM_URGENT,
				      M0_RPC_ITEM_FAILED,
				      M0_RPC_ITEM_REPLIED),
	},
	[M0_RPC_ITEM_URGENT] = {
		.sd_name    = "URGENT",
		.sd_in      = item_entered_in_urgent_state,
		.sd_allowed = M0_BITS(M0_RPC_ITEM_SENDING,
				      M0_RPC_ITEM_FAILED,
				      M0_RPC_ITEM_REPLIED),
	},
	[M0_RPC_ITEM_SENDING] = {
		.sd_name    = "SENDING",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_SENT, M0_RPC_ITEM_FAILED),
	},
	[M0_RPC_ITEM_SENT] = {
		.sd_flags   = M0_SDF_FINAL,
		.sd_name    = "SENT",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_WAITING_FOR_REPLY,
				      M0_RPC_ITEM_ENQUEUED,/*only reply items*/
				      M0_RPC_ITEM_URGENT,
				      M0_RPC_ITEM_UNINITIALISED),
	},
	[M0_RPC_ITEM_WAITING_FOR_REPLY] = {
		.sd_name    = "WAITING_FOR_REPLY",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_REPLIED,
				      M0_RPC_ITEM_FAILED,
				      M0_RPC_ITEM_ENQUEUED,
				      M0_RPC_ITEM_URGENT),
	},
	[M0_RPC_ITEM_REPLIED] = {
		.sd_flags   = M0_SDF_FINAL,
		.sd_name    = "REPLIED",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_UNINITIALISED,
				      M0_RPC_ITEM_ENQUEUED,
				      M0_RPC_ITEM_URGENT,
				      M0_RPC_ITEM_FAILED),
	},
	[M0_RPC_ITEM_FAILED] = {
		.sd_flags   = M0_SDF_FINAL,
		.sd_name    = "FAILED",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_UNINITIALISED),
	},
};

static const struct m0_sm_conf outgoing_item_sm_conf = {
	.scf_name      = "Outgoing-RPC-Item-sm",
	.scf_nr_states = ARRAY_SIZE(outgoing_item_states),
	.scf_state     = outgoing_item_states,
};

static struct m0_sm_state_descr incoming_item_states[] = {
	[M0_RPC_ITEM_UNINITIALISED] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "UNINITIALISED",
		.sd_allowed = 0,
	},
	[M0_RPC_ITEM_INITIALISED] = {
		.sd_flags   = M0_SDF_INITIAL | M0_SDF_FINAL,
		.sd_name    = "INITIALISED",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_ACCEPTED,
				      M0_RPC_ITEM_FAILED,
				      M0_RPC_ITEM_UNINITIALISED),
	},
	[M0_RPC_ITEM_ACCEPTED] = {
		.sd_flags   = M0_SDF_FINAL,
		.sd_name    = "ACCEPTED",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_REPLIED,
				      M0_RPC_ITEM_FAILED,
				      M0_RPC_ITEM_UNINITIALISED),
	},
	[M0_RPC_ITEM_REPLIED] = {
		.sd_flags   = M0_SDF_FINAL,
		.sd_name    = "REPLIED",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_UNINITIALISED),
	},
	[M0_RPC_ITEM_FAILED] = {
		.sd_flags   = M0_SDF_FINAL,
		.sd_name    = "FAILED",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_UNINITIALISED),
	},
};

static const struct m0_sm_conf incoming_item_sm_conf = {
	.scf_name      = "Incoming-RPC-Item-sm",
	.scf_nr_states = ARRAY_SIZE(incoming_item_states),
	.scf_state     = incoming_item_states,
};

M0_INTERNAL bool m0_rpc_item_invariant(const struct m0_rpc_item *item)
{
	int  state;
	bool req;
	bool rply;
	bool oneway;

	if (item == NULL || item->ri_type == NULL)
		return false;

	state  = item->ri_sm.sm_state;
	req    = m0_rpc_item_is_request(item);
	rply   = m0_rpc_item_is_reply(item);
	oneway = m0_rpc_item_is_oneway(item);

	return  item->ri_magic == M0_RPC_ITEM_MAGIC &&
		item->ri_prio >= M0_RPC_ITEM_PRIO_MIN &&
		item->ri_prio <= M0_RPC_ITEM_PRIO_MAX &&
		(req + rply + oneway == 1) && /* only one of three is true */
		equi(req || rply, item->ri_session != NULL) &&

		equi(state == M0_RPC_ITEM_FAILED, item->ri_error != 0) &&

		ergo(item->ri_reply != NULL,
			req &&
			M0_IN(state, (M0_RPC_ITEM_SENDING,
				      M0_RPC_ITEM_WAITING_FOR_REPLY,
				      M0_RPC_ITEM_REPLIED))) &&

		equi(itemq_tlink_is_in(item), state == M0_RPC_ITEM_ENQUEUED) &&
		equi(item->ri_itemq != NULL,  state == M0_RPC_ITEM_ENQUEUED) &&

		equi(packet_item_tlink_is_in(item),
		     state == M0_RPC_ITEM_SENDING);

}

M0_INTERNAL const char *item_state_name(const struct m0_rpc_item *item)
{
	return item->ri_sm.sm_conf->scf_state[item->ri_sm.sm_state].sd_name;
}

M0_INTERNAL const char *item_kind(const struct m0_rpc_item *item)
{
	return  m0_rpc_item_is_request(item) ? "REQUEST" :
		m0_rpc_item_is_reply(item)   ? "REPLY"   :
		m0_rpc_item_is_oneway(item)  ? "ONEWAY"  : "INVALID_KIND";
}

void m0_rpc_item_init(struct m0_rpc_item *item,
		      const struct m0_rpc_item_type *itype)
{
	M0_ENTRY("item: %p", item);
	M0_PRE(item != NULL && itype != NULL);

	item->ri_type  = itype;
	item->ri_magic = M0_RPC_ITEM_MAGIC;
	item->ri_ha_epoch = M0_HA_EPOCH_NONE;

	item->ri_resend_interval = m0_time(M0_RPC_ITEM_RESEND_INTERVAL, 0);
	item->ri_nr_sent_max     = ~(uint64_t)0;

	packet_item_tlink_init(item);
	itemq_tlink_init(item);
        rpcitem_tlink_init(item);
	rpcitem_tlist_init(&item->ri_compound_items);
	m0_sm_timeout_init(&item->ri_deadline_timeout);
	m0_sm_timer_init(&item->ri_timer);
	/* item->ri_sm will be initialised when the item is posted */
	M0_LEAVE();
}
M0_EXPORTED(m0_rpc_item_init);

void m0_rpc_item_fini(struct m0_rpc_item *item)
{
	M0_ENTRY("item: %p", item);

	m0_sm_timer_fini(&item->ri_timer);
	m0_sm_timeout_fini(&item->ri_deadline_timeout);

	if (item->ri_sm.sm_state > M0_RPC_ITEM_UNINITIALISED)
		m0_rpc_item_sm_fini(item);

	if (item->ri_reply != NULL) {
		m0_rpc_item_put(item->ri_reply);
		item->ri_reply = NULL;
	}
	if (itemq_tlink_is_in(item))
		m0_rpc_frm_remove_item(item->ri_frm, item);

	itemq_tlink_fini(item);
	packet_item_tlink_fini(item);
	rpcitem_tlink_fini(item);
	rpcitem_tlist_fini(&item->ri_compound_items);
	M0_LEAVE();
}
M0_EXPORTED(m0_rpc_item_fini);

void m0_rpc_item_get(struct m0_rpc_item *item)
{
	M0_PRE(item != NULL && item->ri_type != NULL &&
	       item->ri_type->rit_ops != NULL &&
	       item->ri_type->rit_ops->rito_item_get != NULL);

	item->ri_type->rit_ops->rito_item_get(item);
}

void m0_rpc_item_put(struct m0_rpc_item *item)
{
	M0_PRE(item != NULL && item->ri_type != NULL &&
	       item->ri_type->rit_ops != NULL &&
	       item->ri_type->rit_ops->rito_item_put != NULL &&
	       item->ri_rmachine != NULL);
	M0_PRE(m0_mutex_is_locked(&item->ri_rmachine->rm_sm_grp.s_lock));

	item->ri_type->rit_ops->rito_item_put(item);
}

m0_bcount_t m0_rpc_item_size(struct m0_rpc_item *item)
{
	if (item->ri_size == 0)
		item->ri_size = m0_rpc_item_onwire_header_size +
				m0_rpc_item_payload_size(item);
	M0_ASSERT(item->ri_size != 0);
	return item->ri_size;
}

m0_bcount_t m0_rpc_item_payload_size(struct m0_rpc_item *item)
{
	M0_PRE(item->ri_type != NULL &&
	       item->ri_type->rit_ops != NULL &&
	       item->ri_type->rit_ops->rito_payload_size != NULL);

	return item->ri_type->rit_ops->rito_payload_size(item);
}

M0_INTERNAL bool m0_rpc_item_is_update(const struct m0_rpc_item *item)
{
	return (item->ri_type->rit_flags & M0_RPC_ITEM_TYPE_MUTABO) != 0;
}

M0_INTERNAL bool m0_rpc_item_is_request(const struct m0_rpc_item *item)
{
	M0_PRE(item != NULL && item->ri_type != NULL);

	return (item->ri_type->rit_flags & M0_RPC_ITEM_TYPE_REQUEST) != 0;
}

M0_INTERNAL bool m0_rpc_item_is_reply(const struct m0_rpc_item *item)
{
	M0_PRE(item != NULL && item->ri_type != NULL);

	return (item->ri_type->rit_flags & M0_RPC_ITEM_TYPE_REPLY) != 0;
}

M0_INTERNAL bool m0_rpc_item_is_oneway(const struct m0_rpc_item *item)
{
	M0_PRE(item != NULL);
	M0_PRE(item->ri_type != NULL);

	return (item->ri_type->rit_flags & M0_RPC_ITEM_TYPE_ONEWAY) != 0;
}

M0_INTERNAL void m0_rpc_item_sm_init(struct m0_rpc_item *item,
				     enum m0_rpc_item_dir dir)
{
	const struct m0_sm_conf *conf;

	M0_PRE(item != NULL && item->ri_rmachine != NULL);

	conf = dir == M0_RPC_ITEM_OUTGOING ? &outgoing_item_sm_conf :
					     &incoming_item_sm_conf;

	M0_LOG(M0_DEBUG, "%p UNINITIALISED -> INITIALISED", item);
	m0_sm_init(&item->ri_sm, conf, M0_RPC_ITEM_INITIALISED,
		   &item->ri_rmachine->rm_sm_grp);
}

M0_INTERNAL void m0_rpc_item_sm_fini(struct m0_rpc_item *item)
{
	M0_PRE(item != NULL);

	m0_sm_fini(&item->ri_sm);
	item->ri_sm.sm_state = M0_RPC_ITEM_UNINITIALISED;
}

M0_INTERNAL void m0_rpc_item_change_state(struct m0_rpc_item *item,
					  enum m0_rpc_item_state state)
{
	M0_PRE(item != NULL);

	M0_LOG(M0_DEBUG, "%p[%s/%u] %s -> %s", item,
	       item_kind(item),
	       item->ri_type->rit_opcode,
	       item_state_name(item),
	       item->ri_sm.sm_conf->scf_state[state].sd_name);

	m0_sm_state_set(&item->ri_sm, state);
}

M0_INTERNAL void m0_rpc_item_failed(struct m0_rpc_item *item, int32_t rc)
{
	M0_PRE(item != NULL && rc != 0);

	M0_ENTRY("FAILED: item: %p error %d", item, rc);

	item->ri_rmachine->rm_stats.rs_nr_failed_items++;
	/*
	 * Request and Reply items take hold on session until
	 * they are SENT/FAILED.
	 * See: m0_rpc__post_locked(), m0_rpc_reply_post()
	 *      m0_rpc_item_send()
	 */
	if (M0_IN(item->ri_sm.sm_state, (M0_RPC_ITEM_ENQUEUED,
					 M0_RPC_ITEM_URGENT,
					 M0_RPC_ITEM_SENDING)) &&
	   (m0_rpc_item_is_request(item) || m0_rpc_item_is_reply(item)))
		m0_rpc_session_release(item->ri_session);

	item->ri_error = rc;
	m0_rpc_item_change_state(item, M0_RPC_ITEM_FAILED);
	m0_rpc_item_stop_timer(item);
	/* XXX ->rio_sent() can be called multiple times (due to cancel). */
	if (m0_rpc_item_is_oneway(item) &&
	    item->ri_ops != NULL && item->ri_ops->rio_sent != NULL) {
		item->ri_ops->rio_sent(item);
	}
	m0_rpc_session_item_failed(item);
	M0_LEAVE();
}

int m0_rpc_item_timedwait(struct m0_rpc_item *item,
			  uint64_t states, m0_time_t timeout)
{
        int rc;

        m0_rpc_machine_lock(item->ri_rmachine);
        rc = m0_sm_timedwait(&item->ri_sm, states, timeout);
        m0_rpc_machine_unlock(item->ri_rmachine);

        return rc;
}

int m0_rpc_item_wait_for_reply(struct m0_rpc_item *item, m0_time_t timeout)
{
	int rc;

	M0_PRE(m0_rpc_item_is_request(item));

	rc = m0_rpc_item_timedwait(item, M0_BITS(M0_RPC_ITEM_REPLIED,
						 M0_RPC_ITEM_FAILED),
				   timeout);
	if (rc == 0 && item->ri_sm.sm_state == M0_RPC_ITEM_FAILED)
		rc = item->ri_error;

	M0_POST(ergo(rc == 0, item->ri_sm.sm_state == M0_RPC_ITEM_REPLIED));
	return rc;
}

void m0_rpc_item_delete(struct m0_rpc_item *item)
{
	struct m0_rpc_machine *mach = item->ri_rmachine;

	M0_PRE(m0_rpc_conn_is_snd(item->ri_session->s_conn));
        m0_rpc_machine_lock(mach);
	M0_PRE(item->ri_sm.sm_state != M0_RPC_ITEM_SENT);

	if (!M0_IN(item->ri_sm.sm_state, (M0_RPC_ITEM_FAILED,
					  M0_RPC_ITEM_REPLIED))) {
		m0_rpc_item_failed(item, -ECANCELED);
		item->ri_error = 0;
	}
	m0_rpc_item_fini(item);
	m0_rpc_item_init(item, item->ri_type);
        m0_rpc_machine_unlock(mach);
}

M0_INTERNAL struct m0_rpc_item *sm_to_item(struct m0_sm *mach)
{
	return container_of(mach, struct m0_rpc_item, ri_sm);
}

static int item_entered_in_urgent_state(struct m0_sm *mach)
{
	struct m0_rpc_item *item;
	struct m0_rpc_frm  *frm;

	item = sm_to_item(mach);
	frm  = item->ri_frm;
	if (item_is_in_waiting_queue(item, frm)) {
		M0_LOG(M0_DEBUG, "%p [%s/%u] ENQUEUED -> URGENT",
		       item, item_kind(item), item->ri_type->rit_opcode);
		m0_rpc_frm_item_deadline_passed(frm, item);
		/*
		 * m0_rpc_frm_item_deadline_passed() might reenter in
		 * m0_sm_state_set() and modify item state.
		 * So at this point the item may or may not be in URGENT state.
		 */
	}
	return -1;
}

M0_INTERNAL int m0_rpc_item_start_timer(struct m0_rpc_item *item)
{
	M0_PRE(m0_rpc_item_is_request(item));

	if (M0_FI_ENABLED("failed")) {
		M0_LOG(M0_DEBUG, "item %p failed to start timer", item);
		return -EINVAL;
	}

	if (item->ri_resend_interval == M0_TIME_NEVER)
		return 0;

	M0_LOG(M0_DEBUG, "item %p Starting timer", item);
	m0_sm_timer_fini(&item->ri_timer);
	m0_sm_timer_init(&item->ri_timer);
	return m0_sm_timer_start(&item->ri_timer, &item->ri_rmachine->rm_sm_grp,
				 item_timer_cb,
				 m0_time_add(m0_time_now(),
					     item->ri_resend_interval));
}

M0_INTERNAL void m0_rpc_item_stop_timer(struct m0_rpc_item *item)
{
	if (m0_sm_timer_is_armed(&item->ri_timer)) {
		M0_ASSERT(m0_rpc_item_is_request(item));
		M0_LOG(M0_DEBUG, "%p Stopping timer", item);
		m0_sm_timer_cancel(&item->ri_timer);
	}
}

static void item_timer_cb(struct m0_sm_timer *timer)
{
	struct m0_rpc_item *item;

	M0_ENTRY();
	M0_PRE(timer != NULL);

	item = container_of(timer, struct m0_rpc_item, ri_timer);
	M0_ASSERT(item->ri_magic == M0_RPC_ITEM_MAGIC);
	M0_ASSERT(m0_rpc_machine_is_locked(item->ri_rmachine));

	M0_LOG(M0_DEBUG, "%p [%s/%u] %s Timer elapsed.", item, item_kind(item),
	       item->ri_type->rit_opcode, item_state_name(item));

	if (item->ri_nr_sent >= item->ri_nr_sent_max)
		item_timedout(item);
	else
		item_resend(item);
}

static void item_timedout(struct m0_rpc_item *item)
{
	M0_LOG(M0_DEBUG, "%p [%s/%u] %s TIMEDOUT.", item, item_kind(item),
	       item->ri_type->rit_opcode, item_state_name(item));
	item->ri_rmachine->rm_stats.rs_nr_timedout_items++;

	switch (item->ri_sm.sm_state) {
	case M0_RPC_ITEM_ENQUEUED:
	case M0_RPC_ITEM_URGENT:
		m0_rpc_item_get(item);
		m0_rpc_frm_remove_item(item->ri_frm, item);
		m0_rpc_item_failed(item, -ETIMEDOUT);
		m0_rpc_item_put(item);
		break;

	case M0_RPC_ITEM_SENDING:
		item->ri_error = -ETIMEDOUT;
		/* item will be moved to FAILED state in item_done() */
		break;

	case M0_RPC_ITEM_WAITING_FOR_REPLY:
		m0_rpc_item_failed(item, -ETIMEDOUT);
		break;

	default:
		M0_ASSERT(false);
	}
	M0_LEAVE();
}

static void item_resend(struct m0_rpc_item *item)
{
	int rc;

	switch (item->ri_sm.sm_state) {
	case M0_RPC_ITEM_ENQUEUED:
	case M0_RPC_ITEM_URGENT:
		rc = m0_rpc_item_start_timer(item);
		/* XXX already completed requests??? */
		if (rc != 0) {
			m0_rpc_item_get(item);
			m0_rpc_frm_remove_item(item->ri_frm, item);
			m0_rpc_item_failed(item, -ETIMEDOUT);
			m0_rpc_item_put(item);
		}
		break;

	case M0_RPC_ITEM_SENDING:
		item->ri_error = m0_rpc_item_start_timer(item);
		break;

	case M0_RPC_ITEM_WAITING_FOR_REPLY:
		m0_rpc_item_send(item);
		break;

	default:
		M0_ASSERT(false);
	}
}

M0_INTERNAL void m0_rpc_item_send(struct m0_rpc_item *item)
{
	uint32_t state = item->ri_sm.sm_state;
	int      rc;

	M0_ENTRY("item: %p", item);
	M0_PRE(item != NULL && m0_rpc_machine_is_locked(item->ri_rmachine));
	M0_PRE(m0_rpc_item_is_request(item) || m0_rpc_item_is_reply(item));
	M0_PRE(ergo(m0_rpc_item_is_request(item),
		    M0_IN(state, (M0_RPC_ITEM_INITIALISED,
				  M0_RPC_ITEM_WAITING_FOR_REPLY,
				  M0_RPC_ITEM_REPLIED,
				  M0_RPC_ITEM_FAILED))) &&
	       ergo(m0_rpc_item_is_reply(item),
		    M0_IN(state, (M0_RPC_ITEM_INITIALISED,
				  M0_RPC_ITEM_SENT,
				  M0_RPC_ITEM_FAILED))));

	if (M0_FI_ENABLED("advance_deadline")) {
		M0_LOG(M0_DEBUG,"%p deadline advanced", item);
		item->ri_deadline = m0_time_from_now(0, 500 * 1000 * 1000);
	}
	if (m0_rpc_item_is_request(item)) {
		rc = m0_rpc_item_start_timer(item);
		if (rc != 0) {
			m0_rpc_item_failed(item, rc);
			M0_LEAVE();
			return;
		}
	}

	item->ri_nr_sent++;
	/*
	 * This hold will be released when the item is SENT or FAILED.
	 * See rpc/frmops.c:item_sent() and m0_rpc_item_failed()
	 */
	m0_rpc_session_hold_busy(item->ri_session);
	m0_rpc_item_get(item);
	m0_rpc_frm_enq_item(&item->ri_session->s_conn->c_rpcchan->rc_frm, item);
	M0_LEAVE();
}

M0_INTERNAL const char *
m0_rpc_item_remote_ep_addr(const struct m0_rpc_item *item)
{
	M0_PRE(item != NULL && item->ri_session != NULL);

	return item->ri_session->s_conn->c_rpcchan->rc_destep->nep_addr;
}

M0_INTERNAL int m0_rpc_item_received(struct m0_rpc_item *item,
				     struct m0_rpc_machine *machine)
{
	struct m0_rpc_item    *req;
	struct m0_rpc_conn    *conn;
	struct m0_rpc_session *sess;
	int rc = 0;

	M0_PRE(item != NULL);
	M0_PRE(m0_rpc_machine_is_locked(machine));

	M0_ENTRY("item=%p xid=%llu machine=%p", item,
	         (unsigned long long)item->ri_header.osr_xid, machine);

	m0_addb_counter_update(&machine->rm_cntr_rcvd_item_sizes,
			       (uint64_t)m0_rpc_item_size(item));
	++machine->rm_stats.rs_nr_rcvd_items;

	if (m0_rpc_item_is_oneway(item)) {
		m0_rpc_item_dispatch(item);
		M0_RETURN(0);
	}

	M0_ASSERT(m0_rpc_item_is_request(item) || m0_rpc_item_is_reply(item));

	if (m0_rpc_item_is_conn_establish(item)) {
		m0_rpc_item_dispatch(item);
		M0_RETURN(0);
	}

	conn = m0_rpc_machine_find_conn(machine, item);
	if (conn == NULL)
		M0_RETURN(-ENOENT);
	sess = m0_rpc_session_search(conn, item->ri_header.osr_session_id);
	if (sess == NULL)
		M0_RETURN(-ENOENT);
	item->ri_session = sess;

	if (m0_rpc_item_is_request(item)) {
		m0_rpc_session_hold_busy(sess);
		rc = m0_rpc_item_dispatch(item);
		if (rc != 0)
			m0_rpc_session_release(sess);
	} else {
		rc = item_reply_received(item, &req);
	}

	M0_RETURN(rc);
}

static int item_reply_received(struct m0_rpc_item *reply,
			       struct m0_rpc_item **req_out)
{
	struct m0_rpc_item     *req;
	int                     rc;

	M0_ENTRY("item_reply: %p", reply);
	M0_PRE(reply != NULL && req_out != NULL);

	*req_out = NULL;

	req = m0_cookie_of(&reply->ri_header.osr_cookie,
	                   struct m0_rpc_item, ri_cookid);
	if (req == NULL) {
		/*
		 * Either it is a duplicate reply and its corresponding request
		 * item is pruned from the item list, or it is a corrupted
		 * reply
		 * XXX This situation is not expected to arise during testing.
		 *     When control reaches this point during testing it might
		 *     be because of a possible bug. So assert.
		 */
		M0_RETURN(-EPROTO);
	}
	rc = req_replied(req, reply);
	if (rc == 0)
		*req_out = req;

	M0_RETURN(rc);
}

static int req_replied(struct m0_rpc_item *req, struct m0_rpc_item *reply)
{
	int rc = 0;

	M0_PRE(req != NULL && reply != NULL);

	if (req->ri_error == -ETIMEDOUT) {
		/*
		 * The reply is valid but too late. Do nothing.
		 */
		M0_LOG(M0_DEBUG, "rply rcvd, timedout req %p [%s/%u]",
			req, item_kind(req), req->ri_type->rit_opcode);
		rc = -EPROTO;
	} else {
		/*
		 * This is valid reply case.
		 */
		m0_rpc_item_get(reply);

		switch (req->ri_sm.sm_state) {
		case M0_RPC_ITEM_ENQUEUED:
		case M0_RPC_ITEM_URGENT:
			m0_rpc_frm_remove_item(
				&req->ri_session->s_conn->c_rpcchan->rc_frm,
				req);
			m0_rpc_item_process_reply(req, reply);
			m0_rpc_session_release(req->ri_session);
			break;

		case M0_RPC_ITEM_SENDING:
			/*
			 * Buffer sent callback is still pending;
			 * postpone reply processing.
			 * item_sent() will process the reply.
			 */
			M0_LOG(M0_DEBUG, "req: %p rply: %p rply postponed",
			       req, reply);
			req->ri_pending_reply = reply;
			break;

		case M0_RPC_ITEM_ACCEPTED:
		case M0_RPC_ITEM_WAITING_FOR_REPLY:
			m0_rpc_item_process_reply(req, reply);
			break;

		case M0_RPC_ITEM_REPLIED:
			/* Duplicate reply. Drop it. */
			req->ri_rmachine->rm_stats.rs_nr_dropped_items++;
			m0_rpc_item_put(reply);
			break;

		default:
			M0_ASSERT(false);
		}
	}
	return rc;
}

M0_INTERNAL void m0_rpc_item_process_reply(struct m0_rpc_item *req,
					   struct m0_rpc_item *reply)
{
	M0_ENTRY("req: %p", req);

	M0_PRE(req != NULL && reply != NULL);
	M0_PRE(m0_rpc_item_is_request(req));
	M0_PRE(M0_IN(req->ri_sm.sm_state, (M0_RPC_ITEM_WAITING_FOR_REPLY,
					   M0_RPC_ITEM_ENQUEUED,
					   M0_RPC_ITEM_URGENT)));

	m0_rpc_item_stop_timer(req);
	req->ri_reply = reply;
	if (req->ri_ops != NULL && req->ri_ops->rio_replied != NULL)
		req->ri_ops->rio_replied(req);

	m0_rpc_item_change_state(req, M0_RPC_ITEM_REPLIED);

	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_item_send_reply(struct m0_rpc_item *req,
					struct m0_rpc_item *reply)
{
	M0_ENTRY("req: %p", req);

	M0_PRE(req != NULL && reply != NULL);
	M0_PRE(m0_rpc_item_is_request(req));
	M0_PRE(M0_IN(req->ri_sm.sm_state, (M0_RPC_ITEM_ACCEPTED)));

	req->ri_reply = reply;
	m0_rpc_item_change_state(req, M0_RPC_ITEM_REPLIED);

	m0_rpc_session_release(req->ri_session);
	reply->ri_header = req->ri_header;
	m0_rpc_item_send(reply);

	M0_LEAVE();
}

/** @} end of rpc group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
