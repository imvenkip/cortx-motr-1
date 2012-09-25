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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 04/28/2011
 */

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_FORMATION
#include "lib/trace.h"
#include "lib/misc.h"    /* C2_SET0 */
#include "lib/memory.h"
#include "lib/tlist.h"
#include "addb/addb.h"
#include "colibri/magic.h"
#include "rpc/rpc_machine.h"
#include "rpc/item.h"
#include "rpc/formation2.h"
#include "rpc/packet.h"
#include "lib/finject.h"       /* C2_FI_ENABLED */

static bool itemq_invariant(const struct c2_tl *q);
static c2_bcount_t itemq_nr_bytes_acc(const struct c2_tl *q);

static struct c2_tl *frm_which_queue(struct c2_rpc_frm        *frm,
				     const struct c2_rpc_item *item);
static bool frm_is_idle(const struct c2_rpc_frm *frm);
static void frm_itemq_insert(struct c2_rpc_frm *frm, struct c2_rpc_item *item);
static void __itemq_insert(struct c2_tl *q, struct c2_rpc_item *new_item);
static void frm_itemq_remove(struct c2_rpc_frm *frm, struct c2_rpc_item *item);
static void frm_balance(struct c2_rpc_frm *frm);
static void frm_filter_timedout_items(struct c2_rpc_frm *frm);
static bool frm_is_ready(const struct c2_rpc_frm *frm);
static void frm_fill_packet(struct c2_rpc_frm *frm, struct c2_rpc_packet *p);
static bool frm_packet_ready(struct c2_rpc_frm *frm, struct c2_rpc_packet *p);
static bool frm_try_to_bind_item(struct c2_rpc_frm  *frm,
				 struct c2_rpc_item *item);
static void frm_try_merging_item(struct c2_rpc_frm  *frm,
				 struct c2_rpc_item *item,
				 c2_bcount_t         limit);

static bool item_less_or_equal(const struct c2_rpc_item *i0,
			       const struct c2_rpc_item *i1);

static c2_bcount_t available_space_in_packet(const struct c2_rpc_packet *p,
					     const struct c2_rpc_frm    *frm);
static bool item_will_exceed_packet_size(const struct c2_rpc_item   *item,
					 const struct c2_rpc_packet *p,
					 const struct c2_rpc_frm    *frm);

static bool item_supports_merging(const struct c2_rpc_item *item);

static bool
constraints_are_valid(const struct c2_rpc_frm_constraints *constraints);

static const char *str_qtype[] = {
	[FRMQ_TIMEDOUT_BOUND]   = "TIMEDOUT_BOUND",
	[FRMQ_TIMEDOUT_UNBOUND] = "TIMEDOUT_UNBOUND",
	[FRMQ_TIMEDOUT_ONE_WAY] = "TIMEDOUT_ONE_WAY",
	[FRMQ_WAITING_UNBOUND]  = "WAITING_UNBOUND",
	[FRMQ_WAITING_BOUND]    = "WAITING_BOUND",
	[FRMQ_WAITING_ONE_WAY]  = "WAITING_ONE_WAY"
};

C2_BASSERT(ARRAY_SIZE(str_qtype) == FRMQ_NR_QUEUES);

static const struct c2_addb_ctx_type frm_addb_ctx_type = {
        .act_name = "rpc-formation-ctx"
};

static const struct c2_addb_loc frm_addb_loc = {
        .al_name = "rpc-formation-loc"
};

#define frm_first_itemq(frm) (&(frm)->f_itemq[0])
#define frm_end_itemq(frm) (&(frm)->f_itemq[ARRAY_SIZE((frm)->f_itemq)])

#define for_each_itemq_in_frm(itemq, frm)  \
for (itemq = frm_first_itemq(frm); \
     itemq < frm_end_itemq(frm); \
     ++itemq)

C2_TL_DESCR_DEFINE(itemq, "rpc_itemq", static, struct c2_rpc_item,
		   ri_iq_link, ri_link_magic, C2_RPC_ITEM_MAGIC,
		   C2_RPC_ITEMQ_HEAD_MAGIC);
C2_TL_DEFINE(itemq, static, struct c2_rpc_item);

static bool frm_invariant(const struct c2_rpc_frm *frm)
{
	const struct c2_tl *q;
	c2_bcount_t         nr_bytes_acc;
	uint64_t            nr_items;

	nr_bytes_acc = 0;
	nr_items     = 0;

	return frm != NULL &&
	       frm->f_magic == C2_RPC_FRM_MAGIC &&
	       frm->f_state > FRM_UNINITIALISED &&
	       frm->f_state < FRM_NR_STATES &&
	       frm->f_ops != NULL &&
	       equi(frm->f_state == FRM_IDLE,  frm_is_idle(frm)) &&
	       c2_forall(i, FRMQ_NR_QUEUES,
			 q             = &frm->f_itemq[i];
			 nr_items     += itemq_tlist_length(q);
			 nr_bytes_acc += itemq_nr_bytes_acc(q);
			 itemq_invariant(q)) &&
	       frm->f_nr_items == nr_items &&
	       frm->f_nr_bytes_accumulated == nr_bytes_acc;
}

static bool itemq_invariant(const struct c2_tl *q)
{
	struct c2_rpc_item *prev;

	return  q != NULL &&
		c2_tl_forall(itemq, item, q,
				prev = itemq_tlist_prev(q, item);
				ergo(prev != NULL,
				     item_less_or_equal(prev, item))
			    );
}

/**
   Defines total order of rpc items in itemq.
 */
static bool item_less_or_equal(const struct c2_rpc_item *i0,
			       const struct c2_rpc_item *i1)
{
	return	i0->ri_prio > i1->ri_prio ||
		(i0->ri_prio == i1->ri_prio &&
		 i0->ri_deadline <= i1->ri_deadline);
}

/**
   Returns sum of on-wire sizes of all the items in q.
 */
static c2_bcount_t itemq_nr_bytes_acc(const struct c2_tl *q)
{
	struct c2_rpc_item *item;
	c2_bcount_t         size;

	size = 0;
	c2_tl_for(itemq, q, item)
		size += c2_rpc_item_size(item);
	c2_tl_endfor;

	return size;
}

void c2_rpc_frm_constraints_get_defaults(struct c2_rpc_frm_constraints *c)
{
	C2_ENTRY();

	/** @todo XXX decide default values for constraints */
	c->fc_max_nr_packets_enqed     = 100;
	c->fc_max_nr_segments          = 128;
	c->fc_max_packet_size          = 4096;
	c->fc_max_nr_bytes_accumulated = 4096;

	C2_LEAVE();
}

static bool
constraints_are_valid(const struct c2_rpc_frm_constraints *constraints)
{
	/** @todo XXX Check whether constraints are consistent */
	return constraints != NULL;
}

static bool frm_is_idle(const struct c2_rpc_frm *frm)
{
	return frm->f_nr_items == 0 && frm->f_nr_packets_enqed == 0;
}

void c2_rpc_frm_init(struct c2_rpc_frm             *frm,
		     struct c2_rpc_frm_constraints *constraints,
		     const struct c2_rpc_frm_ops   *ops)
{
	struct c2_tl *q;

	C2_ENTRY("frm: %p", frm);
	C2_PRE(frm != NULL &&
	       ops != NULL &&
	       constraints_are_valid(constraints));

	C2_SET0(frm);
	frm->f_ops         =  ops;
	frm->f_constraints = *constraints; /* structure instance copy */
	frm->f_magic       =  C2_RPC_FRM_MAGIC;

	for_each_itemq_in_frm(q, frm) {
		itemq_tlist_init(q);
	}

	c2_addb_ctx_init(&frm->f_addb_ctx, &frm_addb_ctx_type,
			 &c2_addb_global_ctx);
	frm->f_state = FRM_IDLE;

	C2_POST(frm_invariant(frm) && frm->f_state == FRM_IDLE);
	C2_LEAVE();
}

void c2_rpc_frm_fini(struct c2_rpc_frm *frm)
{
	struct c2_tl *q;

	C2_ENTRY("frm: %p", frm);
	C2_PRE(frm_invariant(frm));
	C2_LOG(C2_DEBUG, "frm state: %d", frm->f_state);
	C2_PRE(frm->f_state == FRM_IDLE);

	c2_addb_ctx_fini(&frm->f_addb_ctx);
	for_each_itemq_in_frm(q, frm) {
		itemq_tlist_fini(q);
	}

	frm->f_state = FRM_UNINITIALISED;
	frm->f_magic = 0;

	C2_LEAVE();
}

void c2_rpc_frm_enq_item(struct c2_rpc_frm  *frm,
			 struct c2_rpc_item *item)
{
	C2_ENTRY("frm: %p item: %p", frm, item);
	C2_PRE(frm_rmachine_is_locked(frm));
	C2_PRE(frm_invariant(frm) && item != NULL);

	frm_itemq_insert(frm, item);
	frm_balance(frm);

	C2_LEAVE();
}

static void
frm_itemq_insert(struct c2_rpc_frm *frm, struct c2_rpc_item *new_item)
{
	struct c2_tl *q;

	C2_ENTRY("frm: %p item: %p", frm, new_item);
	C2_PRE(new_item != NULL);
	C2_LOG(C2_DEBUG, "priority: %d", new_item->ri_prio);

	q = frm_which_queue(frm, new_item);

	__itemq_insert(q, new_item);

	new_item->ri_itemq = q;
	C2_CNT_INC(frm->f_nr_items);
	frm->f_nr_bytes_accumulated += c2_rpc_item_size(new_item);

	if (frm->f_state == FRM_IDLE)
		frm->f_state = FRM_BUSY;

	C2_LEAVE("nr_items: %llu bytes: %llu",
			(unsigned long long)frm->f_nr_items,
			(unsigned long long)frm->f_nr_bytes_accumulated);
}

/**
   Depending on item->ri_deadline and item->ri_prio returns one of
   frm->f_itemq[], in which the item should be placed.
 */
static struct c2_tl *
frm_which_queue(struct c2_rpc_frm *frm, const struct c2_rpc_item *item)
{
	enum c2_rpc_frm_itemq_type qtype;
	bool                       oneway;
	bool                       bound;
	bool                       deadline_passed;

	C2_ENTRY("item: %p", item);
	C2_PRE(item != NULL);

	oneway          = c2_rpc_item_is_unsolicited(item);
	bound           = oneway ? false : c2_rpc_item_is_bound(item);
	deadline_passed = c2_time_now() >= item->ri_deadline;

	C2_LOG(C2_DEBUG,
		"deadline: [%llu:%llu] bound: %s oneway: %s"
		" deadline_passed: %s",
		(unsigned long long)c2_time_seconds(item->ri_deadline),
		(unsigned long long)c2_time_nanoseconds(item->ri_deadline),
		(char *)c2_bool_to_str(bound), (char *)c2_bool_to_str(oneway),
		(char *)c2_bool_to_str(deadline_passed));

	if (deadline_passed)
		qtype = oneway ? FRMQ_TIMEDOUT_ONE_WAY
			       : bound  ? FRMQ_TIMEDOUT_BOUND
					: FRMQ_TIMEDOUT_UNBOUND;
	else
		qtype = oneway ? FRMQ_WAITING_ONE_WAY
			       : bound  ? FRMQ_WAITING_BOUND
					: FRMQ_WAITING_UNBOUND;
	C2_LEAVE("qtype: %s", str_qtype[qtype]);
	return &frm->f_itemq[qtype];
}

/**
   q is sorted by c2_rpc_item::ri_prio and then by c2_rpc_item::ri_deadline.

   Insert new_item such that the ordering of q is maintained.
 */
static void __itemq_insert(struct c2_tl *q, struct c2_rpc_item *new_item)
{
	struct c2_rpc_item *item;

	C2_ENTRY();

	/* insertion sort. */
	c2_tl_for(itemq, q, item) {
		if (!item_less_or_equal(item, new_item)) {
			itemq_tlink_init(new_item);
			itemq_tlist_add_before(item, new_item);
			break;
		}
	} c2_tl_endfor;
	if (item == NULL)
		itemq_tlink_init_at_tail(new_item, q);

	C2_ASSERT(itemq_invariant(q));
	C2_LEAVE();
}

/**
   Core of formation algorithm.

   @pre frm_rmachine_is_locked(frm)
 */
static void frm_balance(struct c2_rpc_frm *frm)
{
	struct c2_rpc_packet *p;
	int                   packet_count;
	int                   item_count;
	bool                  packet_enqed;

	C2_ENTRY("frm: %p", frm);

	C2_PRE(frm_rmachine_is_locked(frm));
	C2_PRE(frm_invariant(frm));

	C2_LOG(C2_DEBUG, "ready: %s",
	       (char *)c2_bool_to_str(frm_is_ready(frm)));
	packet_count = item_count = 0;

	frm_filter_timedout_items(frm);
	while (frm_is_ready(frm)) {
		C2_ALLOC_PTR_ADDB(p, &frm->f_addb_ctx, &frm_addb_loc);
		if (p == NULL) {
			C2_LOG(C2_ERROR, "Error: packet allocation failed");
			break;
		}
		c2_rpc_packet_init(p);
		frm_fill_packet(frm, p);
		if (c2_rpc_packet_is_empty(p)) {
			/* See FRM_BALANCE_NOTE_1 at the end of this function */
			c2_rpc_packet_fini(p);
			c2_free(p);
			break;
		}
		++packet_count;
		item_count += p->rp_nr_items;
		packet_enqed = frm_packet_ready(frm, p);
		if (packet_enqed) {
			++frm->f_nr_packets_enqed;
			/*
			 * f_nr_packets_enqed will be decremented in packet
			 * done callback, see c2_rpc_frm_packet_done()
			 */
			if (frm->f_state == FRM_IDLE)
				frm->f_state = FRM_BUSY;
		}
	}

	C2_LEAVE("formed %d packet(s) [%d items]", packet_count, item_count);
	C2_ASSERT(frm_invariant(frm));
}
/*
 * FRM_BALANCE_NOTE_1
 * This case can arise if:
 * - Accumulated bytes are >= max_nr_bytes_accumulated,
 *   hence frm is READY AND
 * - All the items in frm are unbound items AND
 * - No slot is available to bind with any of these items.
 */

/**
   Moves all timed-out items from WAITING_* queues to TIMEDOUT_* queues.

   XXX This entire routine is temporary and will be removed in future.
       Currently we don't start any timer for every item. Instead a
       background thread c2_rpc_machine::rm_frm_worker periodically runs
       formation to send any timedout items.
 */
static void frm_filter_timedout_items(struct c2_rpc_frm *frm)
{
	static const enum c2_rpc_frm_itemq_type qtypes[] = {
		FRMQ_WAITING_BOUND,
		FRMQ_WAITING_UNBOUND,
		FRMQ_WAITING_ONE_WAY
	};
	enum c2_rpc_frm_itemq_type  qtype;
	struct c2_rpc_item         *item;
	struct c2_tl               *q;
	int                         i;
	c2_time_t                   now = c2_time_now();

	C2_ENTRY("frm: %p", frm);
	C2_PRE(frm_invariant(frm));

	for (i = 0; i < ARRAY_SIZE(qtypes); ++i) {
		qtype = qtypes[i];
		q     = &frm->f_itemq[qtype];
		c2_tl_for(itemq, q, item) {
			if (item->ri_deadline <= now) {
				frm_itemq_remove(frm, item);
				/*
				 * This time the item will be inserted in
				 * one of TIMEDOUT_* queues.
				 */
				frm_itemq_insert(frm, item);
			}
		} c2_tl_endfor;
	}

	C2_ASSERT(frm_invariant(frm));
	C2_LEAVE();
}

/**
   Is frm ready to form a packet?

   It is possible that frm_is_ready() returns true but no packet could
   be formed. See FRM_BALANCE_NOTE_1
 */
static bool frm_is_ready(const struct c2_rpc_frm *frm)
{
	const struct c2_rpc_frm_constraints *c;
	bool                                 has_timedout_items;

	C2_PRE(frm != NULL);

	has_timedout_items =
		!itemq_tlist_is_empty(&frm->f_itemq[FRMQ_TIMEDOUT_BOUND]) ||
		!itemq_tlist_is_empty(&frm->f_itemq[FRMQ_TIMEDOUT_UNBOUND]) ||
		!itemq_tlist_is_empty(&frm->f_itemq[FRMQ_TIMEDOUT_ONE_WAY]);

	c = &frm->f_constraints;
	return frm->f_nr_packets_enqed < c->fc_max_nr_packets_enqed &&
	       (has_timedout_items ||
		frm->f_nr_bytes_accumulated >= c->fc_max_nr_bytes_accumulated);
}

/**
   Adds RPC items in packet p, taking the constraints into account.

   An item is removed from itemq, once it is added to packet.
 */
static void frm_fill_packet(struct c2_rpc_frm *frm, struct c2_rpc_packet *p)
{
	struct c2_rpc_item *item;
	struct c2_tl       *q;
	c2_bcount_t         limit;
	bool                bound;

	C2_ENTRY("frm: %p packet: %p", frm, p);

	C2_ASSERT(frm_invariant(frm));

	for_each_itemq_in_frm(q, frm) {
		c2_tl_for(itemq, q, item) {
			/* See FRM_FILL_PACKET_NOTE_1 at the end of this func */
			if (available_space_in_packet(p, frm) == 0)
				goto out;
			if (item_will_exceed_packet_size(item, p, frm))
				continue;
			if (c2_rpc_item_is_unbound(item)) {
				bound = frm_try_to_bind_item(frm, item);
				if (!bound) {
					continue;
				}
			}
			C2_ASSERT(c2_rpc_item_is_unsolicited(item) ||
				  c2_rpc_item_is_bound(item));
			frm_itemq_remove(frm, item);
			if (item_supports_merging(item)) {
				limit = available_space_in_packet(p, frm);
				frm_try_merging_item(frm, item, limit);
			}
			C2_ASSERT(!item_will_exceed_packet_size(item, p, frm));
			c2_rpc_packet_add_item(p, item);
		} c2_tl_endfor;
	}

out:
	C2_ASSERT(frm_invariant(frm));
	C2_LEAVE();
	return;
}
/*
 * FRM_FILL_PACKET_NOTE_1
 * I know that this loop is inefficient. But for now
 * let's just stick to simplicity. We can optimize it
 * later if need arises. --Amit
 */

static c2_bcount_t available_space_in_packet(const struct c2_rpc_packet *p,
					     const struct c2_rpc_frm    *frm)
{
	C2_PRE(p->rp_size <= frm->f_constraints.fc_max_packet_size);
	return frm->f_constraints.fc_max_packet_size - p->rp_size;
}

static bool item_will_exceed_packet_size(const struct c2_rpc_item   *item,
					 const struct c2_rpc_packet *p,
					 const struct c2_rpc_frm    *frm)
{
	return c2_rpc_item_size(item) > available_space_in_packet(p, frm);
}

static bool item_supports_merging(const struct c2_rpc_item *item)
{
	C2_PRE(item->ri_type != NULL &&
	       item->ri_type->rit_ops != NULL);

	return item->ri_type->rit_ops->rito_try_merge != NULL;
}

/**
   @see c2_rpc_frm_ops::f_item_bind()
 */
static bool
frm_try_to_bind_item(struct c2_rpc_frm *frm, struct c2_rpc_item *item)
{
	bool result;

	C2_ENTRY("frm: %p item: %p", frm, item);
	C2_PRE(frm != NULL &&
	       item != NULL &&
	       item->ri_session != NULL &&
	       c2_rpc_item_is_unbound(item));
	C2_PRE(frm->f_ops != NULL &&
	       frm->f_ops->fo_item_bind != NULL);
	C2_LOG(C2_DEBUG, "session: %p id: %llu", item->ri_session,
		   (unsigned long long)item->ri_session->s_session_id);

	/* See item_bind() in rpc/frmops.c */
	result = frm->f_ops->fo_item_bind(item);

	C2_LEAVE("result: %s", (char *)c2_bool_to_str(result));
	return result;
}

void
frm_itemq_remove(struct c2_rpc_frm *frm, struct c2_rpc_item *item)
{
	C2_ENTRY("frm: %p item: %p", frm, item);
	C2_PRE(frm != NULL && item != NULL);
	C2_PRE(frm->f_nr_items > 0 && item->ri_itemq != NULL);

	itemq_tlink_del_fini(item);
	item->ri_itemq = NULL;
	C2_CNT_DEC(frm->f_nr_items);
	frm->f_nr_bytes_accumulated -= c2_rpc_item_size(item);
	C2_ASSERT(frm->f_nr_bytes_accumulated >= 0);

	if (frm_is_idle(frm))
		frm->f_state = FRM_IDLE;

	C2_LEAVE();
}

static void frm_try_merging_item(struct c2_rpc_frm  *frm,
				 struct c2_rpc_item *item,
				 c2_bcount_t         limit)
{
	C2_ENTRY("frm: %p item: %p limit: %llu", frm, item,
						 (unsigned long long)limit);
	/** @todo XXX implement item merging */
	C2_LEAVE();
	return;
}

/**
   @see c2_rpc_frm_ops::f_packet_ready()
 */
static bool frm_packet_ready(struct c2_rpc_frm *frm, struct c2_rpc_packet *p)
{
	bool packet_enqed;

	C2_ENTRY("frm: %p packet %p", frm, p);

	C2_PRE(frm != NULL && p != NULL && !c2_rpc_packet_is_empty(p));
	C2_PRE(frm->f_ops != NULL && frm->f_ops->fo_packet_ready != NULL);
	C2_LOG(C2_DEBUG, "nr_items: %llu", (unsigned long long)p->rp_nr_items);

	p->rp_frm = frm;
	/* See packet_ready() in rpc/frmops.c */
	packet_enqed = frm->f_ops->fo_packet_ready(p);

	C2_LEAVE("result: %s", (char *)c2_bool_to_str(packet_enqed));
	return packet_enqed;
}

void c2_rpc_frm_run_formation(struct c2_rpc_frm *frm)
{
	if (C2_FI_ENABLED("do_nothing"))
		return;
	
	C2_ENTRY("frm: %p", frm);
	C2_ASSERT(frm_invariant(frm));
	C2_PRE(frm_rmachine_is_locked(frm));

	frm_balance(frm);

	C2_LEAVE();
}

void c2_rpc_frm_packet_done(struct c2_rpc_packet *p)
{
	struct c2_rpc_frm *frm;

	C2_ENTRY("packet: %p", p);
	C2_ASSERT(c2_rpc_packet_invariant(p));

	frm = p->rp_frm;
	C2_ASSERT(frm_invariant(frm));
	C2_PRE(frm_rmachine_is_locked(frm));

	C2_CNT_DEC(frm->f_nr_packets_enqed);
	C2_LOG(C2_DEBUG, "nr_packets_enqed: %llu",
		(unsigned long long)frm->f_nr_packets_enqed);

	if (frm_is_idle(frm))
		frm->f_state = FRM_IDLE;

	frm_balance(frm);

	C2_LEAVE();
}
