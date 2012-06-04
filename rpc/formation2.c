#include "rpc/rpc2.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_FORMATION
#include "lib/trace.h"
#include "lib/misc.h"    /* C2_SET0 */
#include "lib/memory.h"
#include "rpc/formation2.h"
#include "rpc/packet.h"

#define ULL unsigned long long

struct c2_tl *
frm_which_queue(struct c2_rpc_frm        *frm,
                const struct c2_rpc_item *item);

void frm_itemq_insert(struct c2_rpc_frm *frm, struct c2_rpc_item *item);
void __itemq_insert(struct c2_tl *q, struct c2_rpc_item *new_item);
void start_deadline_timer_if_needed(struct c2_rpc_item *item);
void frm_itemq_remove(struct c2_rpc_frm *frm, struct c2_rpc_item *item);
void __frm_itemq_remove(struct c2_rpc_frm *frm, struct c2_rpc_item *item);
unsigned long item_timer_callback(unsigned long data);
void frm_balance(struct c2_rpc_frm *frm);
void frm_fill_packet(struct c2_rpc_frm *frm, struct c2_rpc_packet *p);
void frm_packet_ready(struct c2_rpc_frm *frm, struct c2_rpc_packet *p);
bool frm_try_to_bind_item(struct c2_rpc_frm *frm, struct c2_rpc_item *item);
void frm_try_merging_item(struct c2_rpc_frm  *frm,
			  struct c2_rpc_item *item,
			  c2_bcount_t         limit);
int item_start_timer(const struct c2_rpc_item *item);
unsigned long item_timer_callback(unsigned long data);

bool constraints_are_valid(const struct c2_rpc_frm_constraints *constraints);

extern struct c2_rpc_frm_ops c2_rpc_frm_default_ops;

#define frm_first_itemq(frm) (&(frm)->f_itemq[0])
#define frm_last_itemq(frm) (&(frm)->f_itemq[ARRAY_SIZE((frm)->f_itemq) - 1])

#define for_each_itemq_in_frm(itemq, frm)  \
for (itemq = frm_first_itemq(frm); \
     itemq <= frm_last_itemq(frm); \
     itemq++)

enum {
	ITEMQ_HEAD_MAGIC = 0x1111000011110000
};

C2_TL_DESCR_DEFINE(itemq, "rpc_itemq", static, struct c2_rpc_item,
		   ri_iq_link, ri_link_magic, C2_RPC_ITEM_FIELD_MAGIC,
		   ITEMQ_HEAD_MAGIC);
C2_TL_DEFINE(itemq, static, struct c2_rpc_item);

bool itemq_invariant(const struct c2_tl *q)
{
	struct c2_rpc_item *prev;
	struct c2_rpc_item *item;
	bool                ok;

	if (q == NULL)
		return false;

	prev = NULL;
	c2_tl_for(itemq, q, item) {
		if (item->ri_frm == NULL)
			return false;
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
	} c2_tl_endfor;
	return true;
}

c2_bcount_t itemq_nr_bytes_acc(const struct c2_tl *q)
{
	struct c2_rpc_item *item;
	c2_bcount_t         size;

	size = 0;
	c2_tl_for(itemq, q, item)
		size += c2_rpc_item_size(item);
	c2_tl_endfor;

	return size;
}
bool frm_invariant(const struct c2_rpc_frm *frm)
{
	const struct c2_tl *q;
	c2_bcount_t         nr_bytes_acc = 0;
	uint64_t            nr_items = 0;

	return frm != NULL &&
	       frm->f_state > FRM_UNINITIALISED &&
	       frm->f_state < FRM_NR_STATES &&
	       frm->f_rmachine != NULL &&
	       ergo(frm->f_state == FRM_IDLE, frm->f_nr_items == 0) &&
	       ergo(frm->f_state == FRM_BUSY, frm->f_nr_items > 0) &&
	       c2_forall(i, FRMQ_NR_QUEUES,
			 q             = &frm->f_itemq[i];
			 nr_items     += itemq_tlist_length(q);
			 nr_bytes_acc += itemq_nr_bytes_acc(q);
			 itemq_invariant(q)) &&
	       frm->f_nr_items == nr_items &&
	       frm->f_nr_bytes_accumulated == nr_bytes_acc;
}

void c2_rpc_frm_constraints_get_defaults(struct c2_rpc_frm_constraints *c)
{
	C2_ENTRY();

	/* XXX Temporary */
	c->fc_max_nr_packets_enqed     = 10000;
	c->fc_max_nr_segments          = 128;
	c->fc_max_packet_size          = 4096;
	c->fc_max_nr_bytes_accumulated = 4096;

	C2_LEAVE();
}

bool constraints_are_valid(const struct c2_rpc_frm_constraints *constraints)
{
	return true;
}

void c2_rpc_frm_init(struct c2_rpc_frm             *frm,
		     struct c2_rpc_machine         *rmachine,
		     struct c2_rpc_chan            *rchan,
		     struct c2_rpc_frm_constraints  constraints,
		     struct c2_rpc_frm_ops         *ops)
{
	struct c2_tl *q;

	C2_ENTRY("frm: %p rmachine %p", frm, rmachine);
	C2_PRE(frm != NULL &&
	       rmachine != NULL &&
	       rchan != NULL &&
	       constraints_are_valid(&constraints));

	C2_SET0(frm);
	frm->f_rmachine    = rmachine;
	frm->f_ops         = ops == NULL ? &c2_rpc_frm_default_ops : ops;
	frm->f_rchan       = rchan;
	frm->f_constraints = constraints; /* structure instance copy */

	for_each_itemq_in_frm(q, frm)
		itemq_tlist_init(q);

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

	for_each_itemq_in_frm(q, frm)
		itemq_tlist_fini(q);

	frm->f_state = FRM_UNINITIALISED;
}

void c2_rpc_frm_enq_item(struct c2_rpc_frm  *frm,
			 struct c2_rpc_item *item)
{
	C2_ENTRY("frm: %p item: %p", frm, item);
	C2_PRE(frm_invariant(frm) && item != NULL);

	frm_itemq_insert(frm, item);
	if (frm->f_state == FRM_IDLE)
		frm->f_state = FRM_BUSY;

	C2_ASSERT(frm_invariant(frm));
	C2_LEAVE("nr_items: %llu bytes: %llu",
			(ULL)frm->f_nr_items,
			(ULL)frm->f_nr_bytes_accumulated);

	frm_balance(frm);
}

const char *str_qtype(enum c2_rpc_frm_itemq_type qtype)
{
	const char *str[] = {
			"TIMEDOUT_BOUND",
			"TIMEDOUT_UNBOUND",
			"TIMEDOUT_ONE_WAY",
			"WAITING_BOUND",
			"WAITING_UNBOUND",
			"WAITING_ONE_WAY"
		   };
	C2_ASSERT(qtype < FRMQ_NR_QUEUES);
	return str[qtype];
}

struct c2_tl *
frm_which_queue(struct c2_rpc_frm        *frm,
	        const struct c2_rpc_item *item)
{
	enum c2_rpc_frm_itemq_type qtype;
	bool                       oneway;
	bool                       bound;
	bool                       deadline_passed;

	C2_ENTRY("item: %p", item);
	C2_PRE(frm_invariant(frm) && item != NULL);

	oneway = c2_rpc_item_is_unsolicited(item);
	bound  = oneway ? false : c2_rpc_item_is_bound(item);
	deadline_passed = /* _now_ is after _deadline_ ??? */
			  c2_time_after_eq(c2_time_now(), item->ri_deadline);

	C2_LOG("deadline: [%llu:%llu] bound: %s oneway: %s deadline_passed: %s",
			(ULL)c2_time_seconds(item->ri_deadline),
			(ULL)c2_time_nanoseconds(item->ri_deadline),
			(char *)bound  ? "true" : "false",
			(char *)oneway ? "true" : "false",
			(char *)deadline_passed ? "true" : "false");

	if (deadline_passed)
		qtype = oneway ? FRMQ_TIMEDOUT_ONE_WAY
			       : bound  ? FRMQ_TIMEDOUT_BOUND
					: FRMQ_TIMEDOUT_UNBOUND;
	else
		qtype = oneway ? FRMQ_WAITING_ONE_WAY
			       : bound  ? FRMQ_WAITING_BOUND
					: FRMQ_WAITING_UNBOUND;
	C2_LEAVE("qtype: %s", str_qtype(qtype));
	return &frm->f_itemq[qtype];
}

/** XXX @todo This should be part of c2_rpc_item invariant */
bool item_priority_is_valid(const struct c2_rpc_item *item)
{
	return item->ri_prio >= C2_RPC_ITEM_PRIO_MIN &&
	       item->ri_prio <= C2_RPC_ITEM_PRIO_MAX;
}

void frm_itemq_insert(struct c2_rpc_frm *frm, struct c2_rpc_item *new_item)
{
	struct c2_tl *q;

	C2_ENTRY("frm: %p item: %p", frm, new_item);
	C2_PRE(new_item != NULL);
	C2_LOG("priority: %d deadline: [%llu:%llu]",
			(int)new_item->ri_prio,
			(ULL)c2_time_seconds(new_item->ri_deadline),
			(ULL)c2_time_nanoseconds(new_item->ri_deadline));

	C2_PRE(item_priority_is_valid(new_item));

	q = frm_which_queue(frm, new_item);

	__itemq_insert(q, new_item);

	new_item->ri_itemq = q;
	new_item->ri_frm   = frm;
	C2_CNT_INC(frm->f_nr_items);
	frm->f_nr_bytes_accumulated += c2_rpc_item_size(new_item);

	start_deadline_timer_if_needed(new_item);

	C2_LEAVE();
	return;
}

void __itemq_insert(struct c2_tl *q, struct c2_rpc_item *new_item)
{
	struct c2_rpc_item *item;

	C2_ENTRY();

	/* where to put the new item in the queue */
	c2_tl_for(itemq, q, item)
		if (item->ri_prio > new_item->ri_prio)
			continue;
	c2_tl_endfor;

	C2_ASSERT(ergo(item != NULL, item->ri_prio <= new_item->ri_prio));

	while (item != NULL &&
	       item->ri_prio == new_item->ri_prio &&
	       c2_time_before_eq(item->ri_deadline,
				 new_item->ri_deadline)) {

		item = itemq_tlist_next(q, item);
	}

	/* insert new item in the queue at its apropriate location */
	if (item == NULL) {
		itemq_tlink_init_at_tail(new_item, q);
	} else {
		C2_ASSERT(item->ri_prio < new_item->ri_prio ||
			  (item->ri_prio == new_item->ri_prio &&
			   c2_time_after(item->ri_deadline,
					 new_item->ri_deadline)));
		itemq_tlist_add_before(item, new_item);
	}

	C2_LEAVE();
}

void start_deadline_timer_if_needed(struct c2_rpc_item *item)
{
	struct c2_rpc_frm *frm;
	struct c2_tl      *q;
	bool               item_is_in_waiting_queue;
	int                err;

	/*
	 * REMEMBER: we cannot again compare "now" and
	 * item->ri_deadline to decide whether to start timer or not.
	 * If item is placed in any of waiting queues then
	 * timer must be started. Then only the item will be able to
	 * transition to one of "timedout" queues.
	 */
	q   = item->ri_itemq;
	frm = item->ri_frm;
	item_is_in_waiting_queue =
		q == &frm->f_itemq[FRMQ_WAITING_BOUND] ||
		q == &frm->f_itemq[FRMQ_WAITING_UNBOUND] ||
		q == &frm->f_itemq[FRMQ_WAITING_ONE_WAY];

	/*
	 * We want ri_timer of all the items to be initialised,
	 * irrespective of whether timer is to be started or not.
	 * Because we will need to check whether timer is running or
	 * not. And we don't want this check to happen on an
	 * uninitialised timer.
	 */
	/** @todo XXX Use HARD timer for rpc-item deadline. */
	c2_timer_init(&item->ri_timer, C2_TIMER_SOFT,
		      item->ri_deadline,
		      item_timer_callback, (unsigned long)item);

	err = 1;
	if (item_is_in_waiting_queue)
		err = c2_timer_start(&item->ri_timer);

	if (err == 0)
		C2_LOG("timer started");
	else
		C2_LOG("timer is not started");
}

void frm_itemq_remove(struct c2_rpc_frm *frm, struct c2_rpc_item *item)
{
	C2_ENTRY("frm: %p item: %p", frm, item);
	C2_PRE(frm != NULL && item != NULL);
	C2_PRE(frm->f_nr_items > 0 && item->ri_itemq != NULL);

	if (c2_timer_is_started(&item->ri_timer)) {
		c2_timer_stop(&item->ri_timer);
		C2_LOG("timer stopped");
	}

	__frm_itemq_remove(frm, item);

	C2_LEAVE();
}

void __frm_itemq_remove(struct c2_rpc_frm *frm, struct c2_rpc_item *item)
{
	C2_ENTRY("frm: %p item: %p", frm, item);
	C2_PRE(frm != NULL && item != NULL);
	C2_PRE(frm->f_nr_items > 0 && item->ri_itemq != NULL);

	itemq_tlink_del_fini(item);
	item->ri_itemq = NULL;
	C2_CNT_DEC(frm->f_nr_items);
	frm->f_nr_bytes_accumulated -= c2_rpc_item_size(item);

	if (frm->f_nr_items == 0)
		frm->f_state = FRM_IDLE;

	C2_LEAVE();
}

unsigned long item_timer_callback(unsigned long data)
{
	struct c2_rpc_item *item;
	struct c2_rpc_frm   *frm;

	C2_ENTRY("data: 0x%lx", data);
	item = (struct c2_rpc_item *)data;
	C2_PRE(item != NULL && item->ri_frm != NULL);

	/** @todo XXX watch out for race here. */
	frm = item->ri_frm;
	c2_rpc_machine_lock(frm->f_rmachine);
	if (item->ri_itemq != NULL) {
		__frm_itemq_remove(frm, item);
		/*
		 * This time the item will be inserted in "timedout"
		 * item-queue and will trigger immediate formation.
		 */
		c2_rpc_frm_enq_item(frm, item);
	}
	c2_rpc_machine_unlock(frm->f_rmachine);

	C2_LEAVE();
	return 0;
}

bool frm_is_ready(const struct c2_rpc_frm *frm)
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
void frm_balance(struct c2_rpc_frm *frm)
{
	struct c2_rpc_packet *p;
	int                   packet_count;
	int                   item_count;

	C2_ENTRY("frm: %p", frm);
	C2_PRE(frm != NULL);
	C2_LOG("ready: %s", frm_is_ready(frm) ? "true" : "false");

	packet_count = item_count = 0;

	while (frm_is_ready(frm)) {
		C2_ALLOC_PTR(p);
		if (p == NULL) {
			C2_LOG("Error: packet allocation failed");
			break;
		}
		c2_rpc_packet_init(p);
		frm_fill_packet(frm, p);
		if (c2_rpc_packet_is_empty(p)) {
			/*
			 * This case can arise if:
			 * - All the items in frm are unbound items AND
			 * - No slot is available to bind with any of these
			      items.
			 */
			c2_rpc_packet_fini(p);
			c2_free(p);
			break;
		}
		++packet_count;
		item_count += p->rp_nr_items;
		frm_packet_ready(frm, p);
	}

	C2_LEAVE("formed %d packet(s) [%d items]", packet_count, item_count);
}

void frm_packet_ready(struct c2_rpc_frm *frm, struct c2_rpc_packet *p)
{
	C2_ENTRY("frm: %p packet %p", frm, p);

	C2_PRE(frm != NULL && p != NULL && !c2_rpc_packet_is_empty(p));
	C2_PRE(frm->f_ops != NULL && frm->f_ops->fo_packet_ready != NULL);
	C2_LOG("nr_items: %llu", (ULL)p->rp_nr_items);

	++frm->f_nr_packets_enqed;
	frm->f_ops->fo_packet_ready(p, frm->f_rmachine, frm->f_rchan);
}

bool frm_try_to_bind_item(struct c2_rpc_frm *frm, struct c2_rpc_item *item)
{
	bool result;

	C2_ENTRY("frm: %p item: %p", frm, item);
	C2_PRE(frm != NULL &&
	       item != NULL &&
	       item->ri_session != NULL &&
	       c2_rpc_item_is_unbound(item));
	C2_PRE(frm->f_ops != NULL &&
	       frm->f_ops->fo_bind_item != NULL);
	C2_LOG("session: %p", item->ri_session);

	result = frm->f_ops->fo_bind_item(item);

	C2_LEAVE("result: %s", result ? "true" : "false");
	return result;
}

void frm_fill_packet(struct c2_rpc_frm *frm, struct c2_rpc_packet *p)
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

	for_each_itemq_in_frm(q, frm) {
		c2_tl_for(itemq, q, item) {
			if (item_will_exceed_packet_size())
				continue;
			if (c2_rpc_item_is_unbound(item)) {
				bound = frm_try_to_bind_item(frm, item);
				if (!bound)
					continue;
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

void frm_try_merging_item(struct c2_rpc_frm  *frm,
			  struct c2_rpc_item *item,
			  c2_bcount_t         limit)
{
	C2_ENTRY("frm: %p item: %p limit: %llu", frm, item, (ULL)limit);
	C2_LEAVE();
	return;
}

void c2_rpc_frm_run_formation(struct c2_rpc_frm *frm)
{
	C2_ENTRY("frm: %p", frm);

	frm_balance(frm);

	C2_LEAVE();
}
