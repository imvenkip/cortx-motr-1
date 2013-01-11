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
#include "mero/magic.h"
#include "rpc/rpc_internal.h"

/**
   @addtogroup rpc

   @{
 */

static int item_entered_in_urgent_state(struct m0_sm *mach);
static void item_timedout_cb(struct m0_sm_timer *timer);
static void item_resend_timer_cb(struct m0_sm_timer *timer);

M0_TL_DESCR_DEFINE(rpcitem, "rpc item tlist", M0_INTERNAL,
		   struct m0_rpc_item, ri_field,
	           ri_magic, M0_RPC_ITEM_MAGIC,
		   M0_RPC_ITEM_HEAD_MAGIC);

M0_TL_DEFINE(rpcitem, M0_INTERNAL, struct m0_rpc_item);

M0_TL_DESCR_DEFINE(rit, "rpc_item_type_descr", static, struct m0_rpc_item_type,
		   rit_linkage,	rit_magic, M0_RPC_ITEM_TYPE_MAGIC,
		   M0_RPC_ITEM_TYPE_HEAD_MAGIC);

M0_TL_DEFINE(rit, static, struct m0_rpc_item_type);

static const struct m0_rpc_onwire_slot_ref invalid_slot_ref = {
	.osr_slot_id    = SLOT_ID_INVALID,
	.osr_sender_id  = SENDER_ID_INVALID,
	.osr_session_id = SESSION_ID_INVALID,
};

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

M0_INTERNAL int m0_rpc_item_type_list_init(void)
{
	M0_ENTRY();

	m0_rwlock_init(&rpc_item_types_lock);
	rit_tlist_init(&rpc_item_types_list);

	M0_RETURN(0);
}

M0_INTERNAL void m0_rpc_item_type_list_fini(void)
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

M0_INTERNAL int m0_rpc_item_type_register(struct m0_rpc_item_type *item_type)
{

	M0_ENTRY("item_type: %p, item_opcode: %u", item_type,
		 item_type->rit_opcode);
	M0_PRE(item_type != NULL);
	M0_PRE(!opcode_is_dup(item_type->rit_opcode));

	m0_rwlock_write_lock(&rpc_item_types_lock);
	rit_tlink_init_at(item_type, &rpc_item_types_list);
	m0_rwlock_write_unlock(&rpc_item_types_lock);

	M0_RETURN(0);
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

static const struct m0_sm_state_descr outgoing_item_states[] = {
	[M0_RPC_ITEM_UNINITIALISED] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "UNINITIALISED",
		.sd_allowed = 0,
	},
	[M0_RPC_ITEM_INITIALISED] = {
		.sd_flags   = M0_SDF_INITIAL | M0_SDF_FINAL,
		.sd_name    = "INITIALISED",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_WAITING_IN_STREAM,
				      M0_RPC_ITEM_ENQUEUED,
				      M0_RPC_ITEM_URGENT,
				      M0_RPC_ITEM_UNINITIALISED),
	},
	[M0_RPC_ITEM_WAITING_IN_STREAM] = {
		.sd_name    = "WAITING_IN_STREAM",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_ENQUEUED),
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

static const struct m0_sm_state_descr incoming_item_states[] = {
	[M0_RPC_ITEM_UNINITIALISED] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "UNINITIALISED",
		.sd_allowed = 0,
	},
	[M0_RPC_ITEM_INITIALISED] = {
		.sd_flags   = M0_SDF_INITIAL | M0_SDF_FINAL,
		.sd_name    = "INITIALISED",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_ACCEPTED,
				      M0_RPC_ITEM_UNINITIALISED),
	},
	[M0_RPC_ITEM_ACCEPTED] = {
		.sd_flags   = M0_SDF_FINAL,
		.sd_name    = "ACCEPTED",
		.sd_allowed = M0_BITS(M0_RPC_ITEM_REPLIED,
				      M0_RPC_ITEM_UNINITIALISED),
	},
	[M0_RPC_ITEM_REPLIED] = {
		.sd_flags   = M0_SDF_FINAL,
		.sd_name    = "REPLIED",
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
	bool bound;

	if (item == NULL || item->ri_type == NULL)
		return false;

	state  = item->ri_sm.sm_state;
	req    = m0_rpc_item_is_request(item);
	rply   = m0_rpc_item_is_reply(item);
	oneway = m0_rpc_item_is_oneway(item);
	bound  = m0_rpc_item_is_bound(item);

	return  item->ri_magic == M0_RPC_ITEM_MAGIC &&
		item->ri_prio >= M0_RPC_ITEM_PRIO_MIN &&
		item->ri_prio <= M0_RPC_ITEM_PRIO_MAX &&
		item->ri_stage >= RPC_ITEM_STAGE_PAST_COMMITTED &&
		item->ri_stage <= RPC_ITEM_STAGE_FUTURE &&
		(req + rply + oneway == 1) && /* only one of three is true */
		equi(req || rply, item->ri_session != NULL) &&

		equi(state == M0_RPC_ITEM_FAILED, item->ri_error != 0) &&
		equi(state == M0_RPC_ITEM_FAILED,
		     M0_IN(item->ri_stage, (RPC_ITEM_STAGE_FAILED,
					    RPC_ITEM_STAGE_TIMEDOUT))) &&

		equi(req && item->ri_error == -ETIMEDOUT,
		     item->ri_stage == RPC_ITEM_STAGE_TIMEDOUT) &&
		equi(req && item->ri_error == -ETIMEDOUT,
		     m0_time_is_in_past(item->ri_op_timeout)) &&

		ergo(item->ri_reply != NULL,
			req &&
			M0_IN(state, (M0_RPC_ITEM_SENDING,
				      M0_RPC_ITEM_WAITING_FOR_REPLY,
				      M0_RPC_ITEM_REPLIED))) &&

		ergo(M0_IN(item->ri_stage, (RPC_ITEM_STAGE_PAST_COMMITTED,
					    RPC_ITEM_STAGE_PAST_VOLATILE)),
		     item->ri_reply != NULL) &&

		ergo(M0_IN(item->ri_stage, (RPC_ITEM_STAGE_FUTURE,
					    RPC_ITEM_STAGE_FAILED,
					    RPC_ITEM_STAGE_TIMEDOUT)),
		     item->ri_reply == NULL) &&

		equi(itemq_tlink_is_in(item), state == M0_RPC_ITEM_ENQUEUED) &&

		equi(item->ri_itemq != NULL,  state == M0_RPC_ITEM_ENQUEUED) &&

		equi(packet_item_tlink_is_in(item),
		     state == M0_RPC_ITEM_SENDING) &&

		ergo(M0_IN(state, (M0_RPC_ITEM_SENDING,
				   M0_RPC_ITEM_SENT,
				   M0_RPC_ITEM_WAITING_FOR_REPLY)),
			ergo(req || rply, bound) &&
			ergo(req,
			     item->ri_stage <= RPC_ITEM_STAGE_IN_PROGRESS)) &&

		ergo(state == M0_RPC_ITEM_REPLIED,
			req && bound &&
			item->ri_reply != NULL &&
			item->ri_stage <= RPC_ITEM_STAGE_PAST_VOLATILE);
}

M0_INTERNAL const char *item_state_name(const struct m0_rpc_item *item)
{
	return item->ri_sm.sm_conf->scf_state[item->ri_sm.sm_state].sd_name;
}

M0_INTERNAL bool item_is_active(const struct m0_rpc_item *item)
{
	return M0_IN(item->ri_stage, (RPC_ITEM_STAGE_IN_PROGRESS,
				      RPC_ITEM_STAGE_FUTURE));
}
M0_INTERNAL struct m0_verno *item_verno(struct m0_rpc_item *item, int idx)
{
	M0_PRE(idx < MAX_SLOT_REF);
	return &item->ri_slot_refs[idx].sr_ow.osr_verno;
}

M0_INTERNAL uint64_t item_xid(struct m0_rpc_item *item, int idx)
{
	M0_PRE(idx < MAX_SLOT_REF);
	return item->ri_slot_refs[idx].sr_ow.osr_xid;
}

M0_INTERNAL const char *item_kind(const struct m0_rpc_item *item)
{
	return  m0_rpc_item_is_request(item) ? "REQUEST" :
		m0_rpc_item_is_reply(item)   ? "REPLY"   :
		m0_rpc_item_is_oneway(item)  ? "ONEWAY"  : "INVALID_KIND";
}

M0_INTERNAL void m0_rpc_item_init(struct m0_rpc_item *item,
				  const struct m0_rpc_item_type *itype)
{
	struct m0_rpc_slot_ref	*sref;

	M0_ENTRY("item: %p", item);
	M0_PRE(item != NULL && itype != NULL);

	M0_SET0(item);

	item->ri_type       = itype;
	item->ri_op_timeout = M0_TIME_NEVER;
	item->ri_magic      = M0_RPC_ITEM_MAGIC;

	item->ri_resend_is_allowed = true;
	item->ri_resend_interval = m0_time(0, 500 * 1000 * 1000);
	m0_sm_timer_init(&item->ri_resend_timer);

	sref = &item->ri_slot_refs[0];
	sref->sr_ow = invalid_slot_ref;

	slot_item_tlink_init(item);
        m0_list_link_init(&item->ri_unbound_link);
	packet_item_tlink_init(item);
        rpcitem_tlink_init(item);
	rpcitem_tlist_init(&item->ri_compound_items);
	m0_sm_timeout_init(&item->ri_deadline_to);
	m0_sm_timer_init(&item->ri_timer);
	/* item->ri_sm will be initialised when the item is posted */
	M0_LEAVE();
}
M0_EXPORTED(m0_rpc_item_init);

M0_INTERNAL void m0_rpc_item_fini(struct m0_rpc_item *item)
{
	struct m0_rpc_slot_ref *sref = &item->ri_slot_refs[0];

	M0_ENTRY("item: %p", item);

	m0_sm_timer_fini(&item->ri_resend_timer);
	m0_sm_timer_fini(&item->ri_timer);
	m0_sm_timeout_fini(&item->ri_deadline_to);

	if (item->ri_sm.sm_state > M0_RPC_ITEM_UNINITIALISED)
		m0_rpc_item_sm_fini(item);

	sref->sr_ow = invalid_slot_ref;
	slot_item_tlink_fini(item);
        m0_list_link_fini(&item->ri_unbound_link);
	packet_item_tlink_fini(item);
	rpcitem_tlink_fini(item);
	rpcitem_tlist_fini(&item->ri_compound_items);
	M0_LEAVE();
}
M0_EXPORTED(m0_rpc_item_fini);

M0_INTERNAL void m0_rpc_item_get(struct m0_rpc_item *item)
{
	M0_PRE(item != NULL && item->ri_type != NULL &&
	       item->ri_type->rit_ops != NULL &&
	       item->ri_type->rit_ops->rito_item_get != NULL);

	item->ri_type->rit_ops->rito_item_get(item);
}

M0_INTERNAL void m0_rpc_item_put(struct m0_rpc_item *item)
{
	M0_PRE(item != NULL && item->ri_type != NULL &&
	       item->ri_type->rit_ops != NULL &&
	       item->ri_type->rit_ops->rito_item_put != NULL);

	item->ri_type->rit_ops->rito_item_put(item);
}

#define ITEM_XCODE_OBJ(ptr)     M0_XCODE_OBJ(m0_rpc_onwire_slot_ref_xc, ptr)
#define SLOT_REF_XCODE_OBJ(ptr) M0_XCODE_OBJ(m0_rpc_item_onwire_header_xc, ptr)

M0_INTERNAL m0_bcount_t m0_rpc_item_onwire_header_size(void)
{
	static m0_bcount_t size = 0;

	if (size == 0) {
		struct m0_rpc_item_onwire_header head;
		struct m0_xcode_ctx              head_xc;
		struct m0_rpc_onwire_slot_ref    slotr;
		struct m0_xcode_ctx              slotr_xc;

		m0_xcode_ctx_init(&head_xc, &ITEM_XCODE_OBJ(&head));
		m0_xcode_ctx_init(&slotr_xc, &SLOT_REF_XCODE_OBJ(&slotr));
		size = m0_xcode_length(&head_xc) + m0_xcode_length(&slotr_xc);
	}

	return size;
}

M0_INTERNAL m0_bcount_t m0_rpc_item_size(struct m0_rpc_item *item)
{
	M0_PRE(item->ri_type != NULL &&
	       item->ri_type->rit_ops != NULL &&
	       item->ri_type->rit_ops->rito_payload_size != NULL);

	if (item->ri_size == 0)
		item->ri_size = m0_rpc_item_onwire_header_size() +
			item->ri_type->rit_ops->rito_payload_size(item);
	M0_ASSERT(item->ri_size != 0);
	return item->ri_size;
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

M0_INTERNAL bool m0_rpc_item_is_bound(const struct m0_rpc_item *item)
{
	M0_PRE(item != NULL);

	return item->ri_slot_refs[0].sr_slot != NULL;
}

M0_INTERNAL bool m0_rpc_item_is_unbound(const struct m0_rpc_item *item)
{
	return !m0_rpc_item_is_bound(item) && !m0_rpc_item_is_oneway(item);
}

M0_INTERNAL void m0_rpc_item_set_stage(struct m0_rpc_item *item,
				       enum m0_rpc_item_stage stage)
{
	bool                   was_active;
	struct m0_rpc_session *session = item->ri_session;

	M0_PRE(m0_rpc_session_invariant(session));

	was_active = item_is_active(item);
	M0_ASSERT(ergo(was_active,
		       session_state(session) == M0_RPC_SESSION_BUSY));

	item->ri_stage = stage;
	m0_rpc_session_mod_nr_active_items(session,
					   item_is_active(item) - was_active);

	M0_POST(m0_rpc_session_invariant(session));
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
		   &item->ri_rmachine->rm_sm_grp, NULL /* addb ctx */);
}

M0_INTERNAL void m0_rpc_item_sm_fini(struct m0_rpc_item *item)
{
	M0_PRE(item != NULL);

	m0_sm_fini(&item->ri_sm);
}

M0_INTERNAL void m0_rpc_item_change_state(struct m0_rpc_item *item,
					  enum m0_rpc_item_state state)
{
	M0_PRE(item != NULL);

	M0_LOG(M0_FATAL, "%p[%s/%u] %s -> %s", item,
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
	 *      m0_rpc_item_resend()
	 */
	if (M0_IN(item->ri_sm.sm_state, (M0_RPC_ITEM_ENQUEUED,
					 M0_RPC_ITEM_URGENT,
					 M0_RPC_ITEM_SENDING)) &&
	   (m0_rpc_item_is_request(item) || m0_rpc_item_is_reply(item)))
		m0_rpc_session_release(item->ri_session);

	item->ri_error = rc;
	m0_rpc_item_change_state(item, M0_RPC_ITEM_FAILED);
	m0_rpc_item_stop_timer(item);
	m0_rpc_session_item_failed(item);

	if (m0_rpc_item_is_request(item) &&
	    item->ri_ops != NULL &&
	    item->ri_ops->rio_replied != NULL)
		item->ri_ops->rio_replied(item);

	M0_LEAVE();
}

M0_INTERNAL int m0_rpc_item_timedwait(struct m0_rpc_item *item,
				      uint64_t states, m0_time_t timeout)
{
        int rc;

        m0_rpc_machine_lock(item->ri_rmachine);
        rc = m0_sm_timedwait(&item->ri_sm, states, timeout);
        m0_rpc_machine_unlock(item->ri_rmachine);

        return rc;
}

M0_INTERNAL int m0_rpc_item_wait_for_reply(struct m0_rpc_item *item,
					   m0_time_t timeout)
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

	if (item->ri_op_timeout != M0_TIME_NEVER) {
		if (m0_time_is_in_past(item->ri_op_timeout))
			M0_LOG(M0_WARN, "item: %p timeout in past", item);
		M0_LOG(M0_DEBUG, "item %p Starting timer", item);
		return m0_sm_timer_start(&item->ri_timer,
					 &item->ri_rmachine->rm_sm_grp,
					 item_timedout_cb,
					 item->ri_op_timeout);
	}
	return 0;
}

M0_INTERNAL void m0_rpc_item_stop_timer(struct m0_rpc_item *item)
{
	if (m0_sm_timer_is_armed(&item->ri_timer)) {
		M0_ASSERT(m0_rpc_item_is_request(item));
		M0_LOG(M0_DEBUG, "%p Stopping timer", item);
		m0_sm_timer_cancel(&item->ri_timer);
	}
}

static void item_timedout_cb(struct m0_sm_timer *timer)
{
	enum m0_rpc_item_state  state;
	struct m0_rpc_item     *item;

	M0_ENTRY();

	M0_PRE(timer != NULL);

	item = container_of(timer, struct m0_rpc_item, ri_timer);
	M0_ASSERT(item->ri_magic == M0_RPC_ITEM_MAGIC);
	M0_ASSERT(m0_rpc_machine_is_locked(item->ri_rmachine));

	M0_LOG(M0_DEBUG, "%p [%s/%u] %s -> TIMEDOUT", item, item_kind(item),
	       item->ri_type->rit_opcode, item_state_name(item));

	item->ri_rmachine->rm_stats.rs_nr_timedout_items++;

	m0_rpc_item_stop_resend_timer(item);

	state = item->ri_sm.sm_state;
	if (M0_IN(state, (M0_RPC_ITEM_ENQUEUED, M0_RPC_ITEM_URGENT))) {
		m0_rpc_item_get(item);
		m0_rpc_frm_remove_item(item->ri_frm, item);
		m0_rpc_item_failed(item, -ETIMEDOUT);
		m0_rpc_item_put(item);
	} else if (state == M0_RPC_ITEM_WAITING_FOR_REPLY) {
		m0_rpc_item_failed(item, -ETIMEDOUT);
	} else if (state == M0_RPC_ITEM_SENDING) {
		item->ri_error = -ETIMEDOUT;
		/* item will be moved to FAILED state in item_done() */
	} else {
		M0_ASSERT(false);
	}
	M0_LEAVE();
}

M0_INTERNAL int m0_rpc_item_start_resend_timer(struct m0_rpc_item *item)
{
	int rc;

	M0_ENTRY();
	M0_PRE(item != NULL && m0_rpc_item_is_request(item));
	rc = m0_sm_timer_start(&item->ri_resend_timer,
				&item->ri_rmachine->rm_sm_grp,
				item_resend_timer_cb,
				m0_time_add(m0_time_now(),
					    item->ri_resend_interval));

	if (rc == 0)
		M0_LOG(M0_FATAL, "item %p [%s/%u] started resend timer",
			item, item_kind(item), item->ri_type->rit_opcode);
	else
		M0_LOG(M0_FATAL,
			"item %p [%s/%u] failed to start resend timer %d",
			item, item_kind(item), item->ri_type->rit_opcode, rc);
	M0_RETURN(rc);
}

M0_INTERNAL void m0_rpc_item_stop_resend_timer(struct m0_rpc_item *item)
{
	if (m0_sm_timer_is_armed(&item->ri_resend_timer)) {
		M0_ASSERT(m0_rpc_item_is_request(item));
		M0_LOG(M0_FATAL, "%p Stopping resend timer", item);
		m0_sm_timer_cancel(&item->ri_resend_timer);
	}
}

static void item_resend_timer_cb(struct m0_sm_timer *timer)
{
	struct m0_rpc_item *item;

	item = container_of(timer, struct m0_rpc_item, ri_resend_timer);
	M0_ASSERT(item->ri_magic == M0_RPC_ITEM_MAGIC);
	M0_ASSERT(m0_rpc_machine_is_locked(item->ri_rmachine));
	M0_ASSERT(m0_rpc_item_is_request(item) &&
		  item->ri_sm.sm_state == M0_RPC_ITEM_WAITING_FOR_REPLY);
	M0_LOG(M0_FATAL, "%p [%s/%u] %s resend timer expired",
		item, item_kind(item), item->ri_type->rit_opcode,
		item_state_name(item));
	m0_sm_timer_init(timer);
	m0_rpc_item_resend(item);
}

M0_INTERNAL void m0_rpc_item_resend(struct m0_rpc_item *item)
{
	uint32_t state = item->ri_sm.sm_state;

	M0_ENTRY("item: %p", item);
	M0_PRE(item != NULL && m0_rpc_machine_is_locked(item->ri_rmachine));
	M0_PRE(m0_rpc_item_is_request(item) || m0_rpc_item_is_reply(item));
	M0_PRE(ergo(m0_rpc_item_is_request(item),
		    M0_IN(state, (M0_RPC_ITEM_WAITING_FOR_REPLY,
				  M0_RPC_ITEM_REPLIED,
				  M0_RPC_ITEM_FAILED))) &&
		    /* XXX think more about sending failed items */
	       ergo(m0_rpc_item_is_reply(item),
		    M0_IN(state, (M0_RPC_ITEM_SENT, M0_RPC_ITEM_FAILED))));

	if (M0_FI_ENABLED("advance_deadline")) {
		M0_LOG(M0_FATAL,"%p deadline advanced", item);
		item->ri_deadline = m0_time_from_now(0, 500 * 1000 * 1000);
	}

	if (item->ri_nr_resend_attempts == 0)
		item->ri_rmachine->rm_stats.rs_nr_resent_items++;
	item->ri_nr_resend_attempts++;
	m0_rpc_session_hold_busy(item->ri_session);
	m0_rpc_frm_enq_item(&item->ri_session->s_conn->c_rpcchan->rc_frm,
			    item);
	M0_LOG(M0_FATAL, "item: %p [%s/%u] enqueued", item, item_kind(item),
		item->ri_type->rit_opcode);
	M0_LEAVE();
}

#undef SLOT_REF_XCODE_OBJ

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
