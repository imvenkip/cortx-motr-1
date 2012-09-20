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

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/tlist.h"
#include "lib/rwlock.h"
#include "lib/misc.h"
#include "lib/errno.h"
#include "rpc/rpc2.h"
#include "rpc/item.h"
#include "rpc/rpc_onwire.h"       /* ITEM_ONWIRE_HEADER_SIZE */
#include "rpc/packet.h"           /* packet_item_tlink_init() */
#include "rpc/session_internal.h" /* c2_rpc_session_item_timedout() */

/**
   @addtogroup rpc_layer_core

   @{
 */

static int item_entered_in_sent_state(struct c2_sm *mach);
static int item_entered_in_timedout_state(struct c2_sm *mach);
static int item_entered_in_failed_state(struct c2_sm *mach);

C2_TL_DESCR_DEFINE(rpcitem, "rpc item tlist", , struct c2_rpc_item, ri_field,
	           ri_link_magic, C2_RPC_ITEM_FIELD_MAGIC,
		   C2_RPC_ITEM_HEAD_MAGIC);

C2_TL_DEFINE(rpcitem, , struct c2_rpc_item);

enum {
	/* Hex ASCII value of "rit_link" */
	RPC_ITEM_TYPE_LINK_MAGIC = 0x7269745f6c696e6b,
	/* Hex ASCII value of "rit_head" */
	RPC_ITEM_TYPE_HEAD_MAGIC = 0x7269745f68656164,
};

C2_TL_DESCR_DEFINE(rit, "rpc_item_type_descr", static, struct c2_rpc_item_type,
		   rit_linkage,	rit_magic, RPC_ITEM_TYPE_LINK_MAGIC,
		   RPC_ITEM_TYPE_HEAD_MAGIC);

C2_TL_DEFINE(rit, static, struct c2_rpc_item_type);

/** Global rpc item types list. */
static struct c2_tl        rpc_item_types_list;
static struct c2_rwlock    rpc_item_types_lock;

/**
  Checks if the supplied opcode has already been registered.
  @param opcode RPC item type opcode.
  @retval true if opcode is a duplicate(already registered)
  @retval false if opcode has not been registered yet.
*/
static bool opcode_is_dup(uint32_t opcode)
{
	C2_PRE(opcode > 0);

	return c2_rpc_item_type_lookup(opcode) != NULL;
}

int c2_rpc_base_init(void)
{
	c2_rwlock_init(&rpc_item_types_lock);
	rit_tlist_init(&rpc_item_types_list);
	return 0;
}

void c2_rpc_base_fini(void)
{
	struct c2_rpc_item_type		*item_type;

	c2_rwlock_write_lock(&rpc_item_types_lock);
	c2_tl_for(rit, &rpc_item_types_list, item_type) {
		rit_tlink_del_fini(item_type);
	} c2_tl_endfor;
	rit_tlist_fini(&rpc_item_types_list);
	c2_rwlock_write_unlock(&rpc_item_types_lock);
	c2_rwlock_fini(&rpc_item_types_lock);
}

int c2_rpc_item_type_register(struct c2_rpc_item_type *item_type)
{

	C2_PRE(item_type != NULL);
	C2_PRE(!opcode_is_dup(item_type->rit_opcode));

	c2_rwlock_write_lock(&rpc_item_types_lock);
	rit_tlink_init_at(item_type, &rpc_item_types_list);
	c2_rwlock_write_unlock(&rpc_item_types_lock);

	return 0;
}

void c2_rpc_item_type_deregister(struct c2_rpc_item_type *item_type)
{
	C2_PRE(item_type != NULL);

	c2_rwlock_write_lock(&rpc_item_types_lock);
	rit_tlink_del_fini(item_type);
	item_type->rit_magic = 0;
	c2_rwlock_write_unlock(&rpc_item_types_lock);
}

struct c2_rpc_item_type *c2_rpc_item_type_lookup(uint32_t opcode)
{
	struct c2_rpc_item_type *item_type;

	c2_rwlock_read_lock(&rpc_item_types_lock);
	c2_tl_for(rit, &rpc_item_types_list, item_type) {
		if (item_type->rit_opcode == opcode) {
			break;
		}
	} c2_tl_endfor;
	c2_rwlock_read_unlock(&rpc_item_types_lock);

	C2_POST(ergo(item_type != NULL, item_type->rit_opcode == opcode));
	return item_type;
}

static const struct c2_sm_state_descr item_state_descr[] = {
	[C2_RPC_ITEM_UNINITIALISED] = {
		.sd_flags   = C2_SDF_TERMINAL,
		.sd_name    = "UNINITIALISED",
		.sd_allowed = 0,
	},
	[C2_RPC_ITEM_INITIALISED] = {
		.sd_flags   = C2_SDF_INITIAL,
		.sd_name    = "INITIALISED",
		.sd_allowed = C2_BITS(C2_RPC_ITEM_WAITING_IN_STREAM,
				      C2_RPC_ITEM_ENQUEUED,
				      C2_RPC_ITEM_ACCEPTED,
				      C2_RPC_ITEM_UNINITIALISED),
	},
	[C2_RPC_ITEM_WAITING_IN_STREAM] = {
		.sd_name    = "WAITING_IN_STREAM",
		.sd_allowed = C2_BITS(C2_RPC_ITEM_ENQUEUED),
	},
	[C2_RPC_ITEM_ENQUEUED] = {
		.sd_name    = "ENQUEUED",
		.sd_allowed = C2_BITS(C2_RPC_ITEM_SENDING),
	},
	[C2_RPC_ITEM_SENDING] = {
		.sd_name    = "SENDING",
		.sd_allowed = C2_BITS(C2_RPC_ITEM_SENT, C2_RPC_ITEM_FAILED),
	},
	[C2_RPC_ITEM_SENT] = {
		.sd_name    = "SENT",
		.sd_in      = item_entered_in_sent_state,
		.sd_allowed = C2_BITS(C2_RPC_ITEM_WAITING_FOR_REPLY,
				      C2_RPC_ITEM_UNINITIALISED),
	},
	[C2_RPC_ITEM_WAITING_FOR_REPLY] = {
		.sd_name    = "WAITING_FOR_REPLY",
		.sd_allowed = C2_BITS(C2_RPC_ITEM_REPLIED,
				      C2_RPC_ITEM_TIMEDOUT),
	},
	[C2_RPC_ITEM_REPLIED] = {
		.sd_name    = "REPLIED",
		.sd_allowed = C2_BITS(C2_RPC_ITEM_UNINITIALISED),
	},
	[C2_RPC_ITEM_ACCEPTED] = {
		.sd_name    = "ACCEPTED",
		.sd_allowed = C2_BITS(C2_RPC_ITEM_REPLIED,
				      C2_RPC_ITEM_UNINITIALISED),
	},
	[C2_RPC_ITEM_TIMEDOUT] = {
		.sd_name    = "TIMEDOUT",
		.sd_in      = item_entered_in_timedout_state,
		.sd_allowed = C2_BITS(C2_RPC_ITEM_FAILED),
	},
	[C2_RPC_ITEM_FAILED] = {
		.sd_name    = "FAILED",
		.sd_in      = item_entered_in_failed_state,
		.sd_allowed = C2_BITS(C2_RPC_ITEM_UNINITIALISED),
	},
};

static const struct c2_sm_conf item_sm_conf = {
	.scf_name      = "RPC-Item-sm",
	.scf_nr_states = C2_RPC_ITEM_NR_STATES,
	.scf_state     = item_state_descr,
};

bool c2_rpc_item_invariant(const struct c2_rpc_item *item)
{
	int  state;
	bool req;
	bool rply;
	bool oneway;
	bool bound;

	if (item == NULL || item->ri_type == NULL)
		return false;

	state  = item->ri_sm.sm_state;
	req    = c2_rpc_item_is_request(item);
	rply   = c2_rpc_item_is_reply(item);
	oneway = c2_rpc_item_is_unsolicited(item);
	bound  = c2_rpc_item_is_bound(item);

	return  item->ri_link_magic == C2_RPC_ITEM_FIELD_MAGIC &&
		item->ri_prio >= C2_RPC_ITEM_PRIO_MIN &&
		item->ri_prio <= C2_RPC_ITEM_PRIO_MAX &&
		item->ri_stage >= RPC_ITEM_STAGE_PAST_COMMITTED &&
		item->ri_stage <= RPC_ITEM_STAGE_FUTURE &&
		(req + rply + oneway == 1) && /* only one of three is true */
		equi(req || rply, item->ri_session != NULL) &&
		item->ri_ops != NULL &&
		item->ri_ops->rio_free != NULL &&

		equi(state == C2_RPC_ITEM_FAILED,
		     item->ri_error != 0 &&
		     C2_IN(item->ri_stage, (RPC_ITEM_STAGE_FAILED,
					    RPC_ITEM_STAGE_TIMEDOUT))) &&

		equi(req && item->ri_error == -ETIMEDOUT,
		     item->ri_stage == RPC_ITEM_STAGE_TIMEDOUT &&
		     c2_time_is_in_past(item->ri_op_timeout)) &&

		ergo(item->ri_reply != NULL,
			req &&
			C2_IN(state, (C2_RPC_ITEM_SENDING,
				      C2_RPC_ITEM_WAITING_FOR_REPLY,
				      C2_RPC_ITEM_REPLIED))) &&

		ergo(C2_IN(item->ri_stage, (RPC_ITEM_STAGE_PAST_COMMITTED,
					    RPC_ITEM_STAGE_PAST_VOLATILE)),
		     item->ri_reply != NULL) &&

		ergo(C2_IN(item->ri_stage, (RPC_ITEM_STAGE_FUTURE,
					    RPC_ITEM_STAGE_FAILED,
					    RPC_ITEM_STAGE_TIMEDOUT)),
		     item->ri_reply == NULL) &&

		equi(itemq_tlink_is_in(item), state == C2_RPC_ITEM_ENQUEUED) &&

		equi(item->ri_itemq != NULL,  state == C2_RPC_ITEM_ENQUEUED) &&

		equi(packet_item_tlink_is_in(item),
		     state == C2_RPC_ITEM_SENDING) &&

		ergo(C2_IN(state, (C2_RPC_ITEM_SENDING,
				   C2_RPC_ITEM_SENT,
				   C2_RPC_ITEM_WAITING_FOR_REPLY)),
			ergo(req || rply, bound) &&
			ergo(req,
			     item->ri_stage <= RPC_ITEM_STAGE_IN_PROGRESS)) &&

		ergo(state == C2_RPC_ITEM_REPLIED,
			req && bound &&
			item->ri_reply != NULL &&
			item->ri_stage <= RPC_ITEM_STAGE_PAST_VOLATILE);
}

void c2_rpc_item_init(struct c2_rpc_item            *item,
		      const struct c2_rpc_item_type *itype)
{
	struct c2_rpc_slot_ref	*sref;

	C2_PRE(item != NULL && itype != NULL);

	C2_SET0(item);

	item->ri_link_magic = C2_RPC_ITEM_FIELD_MAGIC;
	item->ri_type       = itype;
	item->ri_op_timeout = C2_TIME_NEVER;

	sref = &item->ri_slot_refs[0];

	sref->sr_slot_id    = SLOT_ID_INVALID;
	sref->sr_sender_id  = SENDER_ID_INVALID;
	sref->sr_session_id = SESSION_ID_INVALID;

	c2_list_link_init(&sref->sr_link);
	c2_list_link_init(&sref->sr_ready_link);

        c2_list_link_init(&item->ri_unbound_link);

	packet_item_tlink_init(item);
        rpcitem_tlink_init(item);
	rpcitem_tlist_init(&item->ri_compound_items);
	/* item->ri_sm will be initialised when the item is posted */
}
C2_EXPORTED(c2_rpc_item_init);

static bool item_is_dummy(const struct c2_rpc_item *item)
{
	return item->ri_slot_refs[0].sr_verno.vn_lsn == C2_LSN_DUMMY_ITEM &&
	       item->ri_slot_refs[0].sr_verno.vn_vc == 0;
}

void c2_rpc_item_fini(struct c2_rpc_item *item)
{
	struct c2_rpc_slot_ref	*sref;

	/*
	 * c2_rpc_item_free() must have already finalised item->ri_sm
	 * using c2_rpc_item_sm_fini().
	 */
	C2_PRE(item->ri_sm.sm_state == C2_RPC_ITEM_UNINITIALISED);

	sref = &item->ri_slot_refs[0];
	sref->sr_slot_id = SLOT_ID_INVALID;
	c2_list_link_fini(&sref->sr_link);
	c2_list_link_fini(&sref->sr_ready_link);

	sref->sr_sender_id = SENDER_ID_INVALID;
	sref->sr_session_id = SESSION_ID_INVALID;

        c2_list_link_fini(&item->ri_unbound_link);

	packet_item_tlink_fini(item);
	rpcitem_tlink_fini(item);
	rpcitem_tlist_fini(&item->ri_compound_items);

}
C2_EXPORTED(c2_rpc_item_fini);

c2_bcount_t c2_rpc_item_size(const struct c2_rpc_item *item)
{
	C2_PRE(item->ri_type != NULL &&
	       item->ri_type->rit_ops != NULL &&
	       item->ri_type->rit_ops->rito_payload_size != NULL);

	return  item->ri_type->rit_ops->rito_payload_size(item) +
		ITEM_ONWIRE_HEADER_SIZE;
}

void c2_rpc_item_free(struct c2_rpc_item *item)
{
	C2_ASSERT(item->ri_ops != NULL &&
		  item->ri_ops->rio_free != NULL);
	c2_rpc_item_sm_fini(item);
	item->ri_ops->rio_free(item);
}

bool c2_rpc_item_is_update(const struct c2_rpc_item *item)
{
	return (item->ri_type->rit_flags & C2_RPC_ITEM_TYPE_MUTABO) != 0;
}

bool c2_rpc_item_is_request(const struct c2_rpc_item *item)
{
	C2_PRE(item != NULL && item->ri_type != NULL);

	return (item->ri_type->rit_flags & C2_RPC_ITEM_TYPE_REQUEST) != 0;
}

bool c2_rpc_item_is_reply(const struct c2_rpc_item *item)
{
	C2_PRE(item != NULL && item->ri_type != NULL);

	return (item->ri_type->rit_flags & C2_RPC_ITEM_TYPE_REPLY) != 0;
}

bool c2_rpc_item_is_unsolicited(const struct c2_rpc_item *item)
{
	C2_PRE(item != NULL);
	C2_PRE(item->ri_type != NULL);

	return (item->ri_type->rit_flags & C2_RPC_ITEM_TYPE_UNSOLICITED) != 0;
}

bool c2_rpc_item_is_bound(const struct c2_rpc_item *item)
{
	C2_PRE(item != NULL);

	return item->ri_slot_refs[0].sr_slot != NULL;
}

bool c2_rpc_item_is_unbound(const struct c2_rpc_item *item)
{
	return !c2_rpc_item_is_bound(item) && !c2_rpc_item_is_unsolicited(item);
}

void c2_rpc_item_set_stage(struct c2_rpc_item     *item,
			   enum c2_rpc_item_stage  stage)
{
	struct c2_rpc_session *session;
	bool                   item_was_active;

	session = item->ri_session;

	C2_ASSERT(c2_rpc_session_invariant(session));

	item_was_active = item_is_active(item);
	C2_ASSERT(ergo(item_was_active,
		       session->s_state == C2_RPC_SESSION_BUSY));
	item->ri_stage = stage;
	c2_rpc_session_mod_nr_active_items(session,
					item_is_active(item) - item_was_active);

	C2_ASSERT(c2_rpc_session_invariant(session));
}

void c2_rpc_item_sm_init(struct c2_rpc_item *item, struct c2_sm_group *grp)
{
	C2_PRE(item != NULL);

	C2_LOG("%p UNINTIALISED -> INITIALISED", item);
	c2_sm_init(&item->ri_sm, &item_sm_conf, C2_RPC_ITEM_INITIALISED, grp,
		   NULL /* addb ctx */);
}

void c2_rpc_item_sm_fini(struct c2_rpc_item *item)
{
	C2_PRE(item != NULL);

	if (!item_is_dummy(item))
		c2_rpc_item_change_state(item, C2_RPC_ITEM_UNINITIALISED);
	if (item->ri_op_timeout != C2_TIME_NEVER)
		c2_sm_timeout_fini(&item->ri_timeout);
	c2_sm_fini(&item->ri_sm);
}

void c2_rpc_item_change_state(struct c2_rpc_item     *item,
			      enum c2_rpc_item_state  state)
{
	C2_PRE(item != NULL);

	C2_LOG("%p[%s/%u] %s -> %s", item,
	       c2_rpc_item_is_request(item) ? "REQUEST" : "REPLY",
	       item->ri_type->rit_opcode,
	       item_state_descr[item->ri_sm.sm_state].sd_name,
	       item_state_descr[state].sd_name);

	c2_sm_state_set(&item->ri_sm, state);
}

void c2_rpc_item_failed(struct c2_rpc_item *item, int32_t rc)
{
	C2_PRE(item != NULL && rc != 0);

	item->ri_error = rc;
	c2_rpc_item_change_state(item, C2_RPC_ITEM_FAILED);
}

int c2_rpc_item_timedwait(struct c2_rpc_item *item,
                          uint64_t            states,
                          c2_time_t           timeout)
{
        struct c2_rpc_machine *machine = item_machine(item);
        int                    rc;

        c2_rpc_machine_lock(machine);
        rc = c2_sm_timedwait(&item->ri_sm, states, timeout);
        c2_rpc_machine_unlock(machine);

        return rc;
}

int c2_rpc_item_wait_for_reply(struct c2_rpc_item *item, c2_time_t timeout)
{
	int rc;

	C2_PRE(c2_rpc_item_is_request(item));

	rc = c2_rpc_item_timedwait(item, C2_BITS(C2_RPC_ITEM_REPLIED, C2_RPC_ITEM_FAILED), timeout);
	if (rc == 0) {
		if (item->ri_sm.sm_state == C2_RPC_ITEM_FAILED)
			rc = item->ri_error;
	}

	C2_POST(ergo(rc == 0, item->ri_sm.sm_state == C2_RPC_ITEM_REPLIED));
	return rc;
}

struct c2_rpc_item *sm_to_item(struct c2_sm *mach)
{
	return container_of(mach, struct c2_rpc_item, ri_sm);
}

static int item_entered_in_sent_state(struct c2_sm *mach)
{
	struct c2_rpc_item *item;

	item = sm_to_item(mach);
	if (c2_rpc_item_is_request(item)) {
		C2_LOG("%p [REQUEST/%u] SENT -> WAITING_FOR_REPLY",
		       item, item->ri_type->rit_opcode);
		return C2_RPC_ITEM_WAITING_FOR_REPLY;
	} else {
		return -1;
	}
}

static int item_entered_in_timedout_state(struct c2_sm *mach)
{
	struct c2_rpc_item *item;

	item = sm_to_item(mach);
	C2_LOG("%p [%u] -> TIMEDOUT", item, item->ri_type->rit_opcode);
	item->ri_error = -ETIMEDOUT;
	c2_sm_timeout_fini(&item->ri_timeout);

	return C2_RPC_ITEM_FAILED;
}

static int item_entered_in_failed_state(struct c2_sm *mach)
{
	struct c2_rpc_item *item;

	item = sm_to_item(mach);
	C2_LOG("%p [%u] FAILED rc: %d\n", item, item->ri_type->rit_opcode,
	       item->ri_error);

	C2_PRE(item->ri_error != 0);
	item->ri_reply = NULL;

	C2_ASSERT(item->ri_ops != NULL);
	if (c2_rpc_item_is_request(item) &&
	    item->ri_ops->rio_replied != NULL)
		item->ri_ops->rio_replied(item);

	c2_rpc_session_item_failed(item);

	return -1;
}

int c2_rpc_item_start_timer(struct c2_rpc_item *item)
{
	if (item->ri_op_timeout != C2_TIME_NEVER) {
		C2_LOG("%p Starting timer", item);
		return c2_sm_timeout(&item->ri_sm, &item->ri_timeout,
				     item->ri_op_timeout, C2_RPC_ITEM_TIMEDOUT);
	}
	return 0;
}

/** @} end of rpc-layer-core group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
