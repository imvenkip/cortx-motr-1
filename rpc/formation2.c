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

#include "rpc/rpc2.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_FORMATION
#include "lib/trace.h"
#include "lib/misc.h"    /* C2_SET0 */
#include "lib/memory.h"
#include "rpc/formation2.h"
#include "rpc/packet.h"

#define ULL unsigned long long

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

static bool
constraints_are_valid(const struct c2_rpc_frm_constraints *constraints);

extern struct c2_rpc_frm_ops c2_rpc_frm_default_ops;

static const char *str_qtype[] = {
			"TIMEDOUT_BOUND",
			"TIMEDOUT_UNBOUND",
			"TIMEDOUT_ONE_WAY",
			"WAITING_BOUND",
			"WAITING_UNBOUND",
			"WAITING_ONE_WAY"
		   };

static const char *bool_to_str(bool b)
{
	return b ? "true" : "false";
}

#define frm_first_itemq(frm) (&(frm)->f_itemq[0])
#define frm_last_itemq(frm) (&(frm)->f_itemq[ARRAY_SIZE((frm)->f_itemq) - 1])

#define for_each_itemq_in_frm(itemq, frm)  \
for (itemq = frm_first_itemq(frm); \
     itemq <= frm_last_itemq(frm); \
     itemq++)

enum {
	ITEMQ_HEAD_MAGIC = 0x4954454d514844, /* ITEMQHD */
	/** value of c2_rpc_frm::f_magic */
	FRM_MAGIC        = 0x5250435f46524d, /* RPC_FRM */
};

C2_TL_DESCR_DEFINE(itemq, "rpc_itemq", static, struct c2_rpc_item,
		   ri_iq_link, ri_link_magic, C2_RPC_ITEM_FIELD_MAGIC,
		   ITEMQ_HEAD_MAGIC);
C2_TL_DEFINE(itemq, static, struct c2_rpc_item);

static bool frm_invariant(const struct c2_rpc_frm *frm)
{
	const struct c2_tl *q;
	c2_bcount_t         nr_bytes_acc;
	uint64_t            nr_items;

	nr_bytes_acc = 0;
	nr_items     = 0;

	return frm != NULL &&
	       frm->f_magic == FRM_MAGIC &&
	       frm->f_state > FRM_UNINITIALISED &&
	       frm->f_state < FRM_NR_STATES &&
	       frm->f_rmachine != NULL &&
	       frm->f_ops != NULL &&
	       frm->f_rchan != NULL &&
	       ergo(frm->f_state == FRM_IDLE,  frm_is_idle(frm)) &&
	       ergo(frm->f_state == FRM_BUSY, !frm_is_idle(frm)) &&
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
	struct c2_rpc_item *item;
	bool                ok;

	if (q == NULL)
		return false;

	prev = NULL;
	c2_tl_for(itemq, q, item) {
		if (prev == NULL) {
			prev = item;
			continue;
		}
		ok = prev->ri_prio >= item->ri_prio &&
		     ergo(prev->ri_prio == item->ri_prio,
			  c2_time_before_eq(prev->ri_deadline,
					    item->ri_deadline));
		if (!ok)
			return false;
		prev = item;
	} c2_tl_endfor;
	return true;
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

	/* XXX Temporary */
	c->fc_max_nr_packets_enqed     = 100;
	c->fc_max_nr_segments          = 128;
	c->fc_max_packet_size          = 4096;
	c->fc_max_nr_bytes_accumulated = 4096;

	C2_LEAVE();
}

static bool
constraints_are_valid(const struct c2_rpc_frm_constraints *constraints)
{
	return true;
}

static bool frm_is_idle(const struct c2_rpc_frm *frm)
{
	return frm->f_nr_items == 0 && frm->f_nr_packets_enqed == 0;
}

void c2_rpc_frm_init(struct c2_rpc_frm             *frm,
		     struct c2_rpc_machine         *rmachine,
		     struct c2_rpc_chan            *rchan,
		     struct c2_rpc_frm_constraints  constraints,
		     struct c2_rpc_frm_ops         *ops)
{
	struct c2_tl *q;

	C2_ENTRY("frm: %p rmachine %p", frm, rmachine);
	C2_PRE(frm      != NULL &&
	       rmachine != NULL &&
	       rchan    != NULL &&
	       ops      != NULL &&
	       constraints_are_valid(&constraints));

	C2_SET0(frm);
	frm->f_rmachine    = rmachine;
	frm->f_ops         = ops;
	frm->f_rchan       = rchan;
	frm->f_constraints = constraints; /* structure instance copy */
	frm->f_magic       = FRM_MAGIC;

	for_each_itemq_in_frm(q, frm) {
		itemq_tlist_init(q);
	}

	frm->f_state = FRM_IDLE;

	C2_POST(frm_invariant(frm) && frm->f_state == FRM_IDLE);
	C2_LEAVE();
}

void c2_rpc_frm_fini(struct c2_rpc_frm *frm)
{
	struct c2_tl *q;

	C2_ENTRY("frm: %p", frm);
	C2_ASSERT(frm_invariant(frm));
	C2_LOG("frm state: %d", frm->f_state);
	C2_PRE(frm->f_state == FRM_IDLE);

	for_each_itemq_in_frm(q, frm) {
		itemq_tlist_fini(q);
	}

	frm->f_state = FRM_UNINITIALISED;
	frm->f_magic = 0;
}

void c2_rpc_frm_enq_item(struct c2_rpc_frm  *frm,
			 struct c2_rpc_item *item)
{
	C2_ENTRY("frm: %p item: %p", frm, item);
	C2_PRE(c2_rpc_machine_is_locked(frm->f_rmachine));
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
	C2_LOG("priority: %d", new_item->ri_prio);

	q = frm_which_queue(frm, new_item);

	__itemq_insert(q, new_item);

	new_item->ri_itemq = q;
	C2_CNT_INC(frm->f_nr_items);
	frm->f_nr_bytes_accumulated += c2_rpc_item_size(new_item);

	if (frm->f_state == FRM_IDLE)
		frm->f_state = FRM_BUSY;

	C2_LEAVE("nr_items: %llu bytes: %llu",
			(ULL)frm->f_nr_items,
			(ULL)frm->f_nr_bytes_accumulated);
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
	deadline_passed = c2_time_after_eq(c2_time_now(), item->ri_deadline);

	C2_LOG("deadline: [%llu:%llu] bound: %s oneway: %s deadline_passed: %s",
			(ULL)c2_time_seconds(item->ri_deadline),
			(ULL)c2_time_nanoseconds(item->ri_deadline),
			bool_to_str(bound), bool_to_str(oneway),
			bool_to_str(deadline_passed));

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

	/* Skip all items whose priority is higher than priority of new item. */
	c2_tl_for(itemq, q, item)
		if (item->ri_prio <= new_item->ri_prio)
			break;
	c2_tl_endfor;

	C2_ASSERT(ergo(item != NULL, item->ri_prio <= new_item->ri_prio));

	/*
	 * Skip all equal priority items whose deadline preceeds
	 * new item's deadline.
	 */
	while (item != NULL &&
	       item->ri_prio == new_item->ri_prio &&
	       c2_time_before_eq(item->ri_deadline,
				 new_item->ri_deadline)) {

		item = itemq_tlist_next(q, item);
	}

	if (item == NULL) {
		C2_ASSERT(itemq_tlist_is_empty(q) ||
			  c2_tl_forall(itemq, tmp, q,
					tmp->ri_prio > new_item->ri_prio ||
					(tmp->ri_prio == new_item->ri_prio &&
					 c2_time_after_eq(new_item->ri_deadline,
						          tmp->ri_deadline))));
		itemq_tlink_init_at_tail(new_item, q);
	} else {
		C2_ASSERT(item->ri_prio < new_item->ri_prio ||
			  (item->ri_prio == new_item->ri_prio &&
			   c2_time_after(item->ri_deadline,
					 new_item->ri_deadline)));
		itemq_tlink_init(new_item);
		itemq_tlist_add_before(item, new_item);
	}

	C2_ASSERT(itemq_invariant(q));
	C2_LEAVE();
}

/**
   Core of formation algorithm.
 */
static void frm_balance(struct c2_rpc_frm *frm)
{
	struct c2_rpc_packet *p;
	int                   packet_count;
	int                   item_count;
	bool                  packet_enqed;

	C2_ENTRY("frm: %p", frm);
	C2_PRE(frm_invariant(frm));
	C2_LOG("ready: %s", bool_to_str(frm_is_ready(frm)));

	packet_count = item_count = 0;

	frm_filter_timedout_items(frm);
	while (frm_is_ready(frm)) {
		C2_ALLOC_PTR(p);
		if (p == NULL) {
			C2_LOG("Error: packet allocation failed");
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
 * - No slot is available to bind with any of these
    items.
 */

static bool item_timedout(const struct c2_rpc_item *item)
{
	return c2_time_after_eq(c2_time_now(), item->ri_deadline);
}

/**
   Moves all timed-out items from WAITING_* queues to TIMEDOUT_* queues.

   XXX This entire routine is temporary and will be removed in future.
       Currently we don't start any timer for every item. Instead a
       background thread c2_rpc_machine::rm_frm_worker periodically runs
       formation to send any timedout items.
 */
static void frm_filter_timedout_items(struct c2_rpc_frm *frm)
{
	enum c2_rpc_frm_itemq_type  qtypes[] = {
						   FRMQ_WAITING_BOUND,
						   FRMQ_WAITING_UNBOUND,
						   FRMQ_WAITING_ONE_WAY
					       };
	enum c2_rpc_frm_itemq_type  qtype;
	struct c2_rpc_item         *item;
	struct c2_tl               *q;
	int                         i;

	C2_ENTRY("frm: %p", frm);
	C2_PRE(frm_invariant(frm));

	for (i = 0; i < ARRAY_SIZE(qtypes); i++) {
		qtype = qtypes[i];
		q     = &frm->f_itemq[qtype];
		c2_tl_for(itemq, q, item) {
			if (item_timedout(item)) {
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

	/* Nested function definitions */
	c2_bcount_t available_space_in_packet(void)
	{
		C2_PRE(p->rp_size <= frm->f_constraints.fc_max_packet_size);
		return frm->f_constraints.fc_max_packet_size - p->rp_size;
	}

	bool item_will_exceed_packet_size(void) {
		return c2_rpc_item_size(item) > available_space_in_packet();
	}

	bool item_supports_merging(void)
	{
		C2_PRE(item->ri_type != NULL &&
		       item->ri_type->rit_ops != NULL);

		return item->ri_type->rit_ops->rito_try_merge != NULL;
	}

	C2_ENTRY("frm: %p packet: %p", frm, p);

	C2_ASSERT(frm_invariant(frm));

	for_each_itemq_in_frm(q, frm) {
		c2_tl_for(itemq, q, item) {
			/* See FRM_FILL_PACKET_NOTE_1 at the end of this func */
			if (available_space_in_packet() == 0)
				break;
			if (item_will_exceed_packet_size())
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
			if (item_supports_merging()) {
				limit = available_space_in_packet();
				frm_try_merging_item(frm, item, limit);
			}
			C2_ASSERT(!item_will_exceed_packet_size());
			c2_rpc_packet_add_item(p, item);
		} c2_tl_endfor;
	}

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
	C2_LOG("session: %p id: %llu", item->ri_session,
				       (ULL)item->ri_session->s_session_id);

	result = frm->f_ops->fo_item_bind(item);

	C2_LEAVE("result: %s", bool_to_str(result));
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

	if (frm_is_idle(frm))
		frm->f_state = FRM_IDLE;

	C2_LEAVE();
}

static void frm_try_merging_item(struct c2_rpc_frm  *frm,
				 struct c2_rpc_item *item,
				 c2_bcount_t         limit)
{
	C2_ENTRY("frm: %p item: %p limit: %llu", frm, item, (ULL)limit);
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
	C2_LOG("nr_items: %llu", (ULL)p->rp_nr_items);

	p->rp_frm = frm;
	packet_enqed = frm->f_ops->fo_packet_ready(p, frm->f_rmachine,
						      frm->f_rchan);

	C2_LEAVE("result: %s", bool_to_str(packet_enqed));
	return packet_enqed;
}

void c2_rpc_frm_run_formation(struct c2_rpc_frm *frm)
{
	C2_ENTRY("frm: %p", frm);
	C2_ASSERT(frm_invariant(frm));
	C2_ASSERT(c2_rpc_machine_is_locked(frm->f_rmachine));

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

	C2_CNT_DEC(frm->f_nr_packets_enqed);
	C2_LOG("nr_packets_enqed: %llu", (ULL)frm->f_nr_packets_enqed);

	if (frm_is_idle(frm))
		frm->f_state = FRM_IDLE;

	frm_balance(frm);

	C2_LEAVE();
}
