#include "rpc/formation2_internal.h"
#include "rpc/formation2.h"
#include "rpc/rpc2.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_FORMATION
#include "lib/trace.h"
#include "lib/misc.h"    /* C2_SET0 */

#define ULL unsigned long long

enum {
	ITEMQ_HEAD_MAGIC = 0x1111000011110000
};

C2_TL_DESCR_DEFINE(rpc_item, "rpc_item", static, struct c2_rpc_item,
		   ri_iq_link, ri_link_magic, C2_RPC_ITEM_FIELD_MAGIC,
		   ITEMQ_HEAD_MAGIC);
C2_TL_DEFINE(rpc_item, static, struct c2_rpc_item);

bool itemq_invariant(const struct itemq *itemq)
{
	return itemq != NULL;
}

bool frm_invariant(const struct c2_rpc_frm *frm)
{
	return frm != NULL &&
	       frm->f_state > FRM_UNINITIALISED &&
	       frm->f_state < FRM_NR_STATES &&
	       frm->f_rmachine != NULL &&
	       ergo(frm->f_state == FRM_IDLE, frm->f_nr_items == 0) &&
	       ergo(frm->f_state == FRM_BUSY, frm->f_nr_items > 0) &&
	       c2_forall(i, FRMQ_NR_QUEUES, itemq_invariant(&frm->f_itemq[i]));
}

int c2_rpc_frm_init(struct c2_rpc_frm             *frm,
		    struct c2_rpc_machine         *rmachine,
		    struct c2_rpc_frm_constraints  constraints)
{
	struct itemq *q;

	C2_ENTRY("frm: %p rmachine %p", frm, rmachine);
	C2_PRE(frm != NULL &&
	       rmachine != NULL &&
	       constraints_are_valid(&constraints));

	C2_SET0(frm);
	frm->f_rmachine    = rmachine;
	frm->f_constraints = constraints; /* structure instance copy */

	for_each_itemq_in_frm(q, frm)
		itemq_init(q);

	frm->f_state = FRM_IDLE;

	C2_POST(frm_invariant(frm) && frm->f_state == FRM_IDLE);
	C2_LEAVE("rc: 0");
	return 0;
}

void c2_rpc_frm_fini(struct c2_rpc_frm *frm)
{
	struct itemq *itemq;

	C2_ENTRY("frm: %p", frm);
	C2_ASSERT(frm_invariant(frm));
	C2_LOG("frm state: %d", frm->f_state);
	C2_PRE(frm->f_state == FRM_IDLE);

	for_each_itemq_in_frm(itemq, frm)
		itemq_fini(itemq);

	frm->f_state = FRM_UNINITIALISED;
}

void c2_rpc_frm_enq_item(struct c2_rpc_frm  *frm,
			 struct c2_rpc_item *item)
{
	struct itemq *q;

	C2_ENTRY("frm: %p item: %p", frm, item);
	C2_PRE(frm_invariant(frm) && item != NULL);

	q = frm_which_queue(frm, item);
	itemq_add(q, item);
	C2_CNT_INC(frm->f_nr_items);
	if (frm->f_state == FRM_IDLE)
		frm->f_state = FRM_BUSY;
	C2_ASSERT(frm_invariant(frm));
}

struct itemq*
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
	deadline_passed = c2_time_eq(item->ri_deadline, c2_time(0, 0)) ||
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
	C2_LEAVE("qtype: %d", qtype);
	return &frm->f_itemq[qtype];
}

void itemq_init(struct itemq *q)
{
	struct c2_tl *list;

	C2_ENTRY("q: %p", q);

	C2_PRE(q != NULL);

	q->iq_nr_items = 0;
	q->iq_accumulated_bytes  = 0;

	for_each_list_in_itemq(list, q)
		rpc_item_tlist_init(list);

	C2_LEAVE("");
}

void itemq_fini(struct itemq *q)
{
	struct c2_tl *list;

	C2_ENTRY("q: %p", q);
	C2_PRE(q != NULL);

	C2_LOG("nr_items: %llu size: %llu", (ULL)q->iq_nr_items,
					    (ULL)q->iq_accumulated_bytes);
	C2_PRE(q->iq_nr_items == 0 &&
	       q->iq_accumulated_bytes == 0);

	for_each_list_in_itemq(list, q)
		rpc_item_tlist_fini(list);

	C2_LEAVE("");
}

/** XXX @todo This should be part of c2_rpc_item invariant */
bool item_priority_is_valid(const struct c2_rpc_item *item)
{
	return item->ri_prio >= 0 && item->ri_prio < ITEM_PRIO_NR;
}

bool constraints_are_valid(const struct c2_rpc_frm_constraints *constraints)
{
	return true;
}

void itemq_add(struct itemq *q, struct c2_rpc_item *new_item)
{
	struct c2_rpc_item *item;
	struct c2_tl       *list;

	C2_ENTRY("q: %p item: %p", q, new_item);
	C2_PRE(itemq_invariant(q) && new_item != NULL);
	C2_LOG("priority: %d", (int)new_item->ri_prio);
	C2_PRE(item_priority_is_valid(new_item));

	list = &q->iq_lists[(int)new_item->ri_prio];

	c2_tl_for(rpc_item, list, item) {
		if (c2_time_after(item->ri_deadline, new_item->ri_deadline))
			break;
	} c2_tl_endfor;

	rpc_item_tlink_init(new_item);
	if (item != NULL)
		rpc_item_tlist_add_before(item, new_item);
	else
		rpc_item_tlist_add_tail(list, new_item);

	new_item->ri_itemq = q;
	q->iq_accumulated_bytes += c2_rpc_item_size(new_item);
	++q->iq_nr_items;

	C2_ASSERT(itemq_invariant(q));
	C2_LEAVE("");
}

bool itemq_is_empty(const struct itemq *q)
{
	return q->iq_nr_items == 0;
}

void itemq_iterator_init(struct itemq_iterator *it,
			 struct itemq          *q)
{
	struct c2_tl *list;

	C2_PRE(it != NULL && q != NULL);

	it->ii_itemq = q;
	it->ii_nr_items_scanned = 0;
	it->ii_curr_list = NULL;
	for_each_list_in_itemq(list, q) {
		if (!rpc_item_tlist_is_empty(list)) {
			it->ii_curr_list = list;
			break;
		}
	}
	if (it->ii_curr_list != NULL) {
		C2_ASSERT(list == it->ii_curr_list &&
			  !rpc_item_tlist_is_empty(list));
		it->ii_curr_item = NULL;
		it->ii_next_item = rpc_item_tlist_head(list);
	} else {
		it->ii_curr_item = NULL;
		it->ii_next_item = NULL;
	}
}
void itemq_iterator_fini(struct itemq_iterator *it)
{
	C2_PRE(it->ii_itemq != NULL);
	C2_SET0(it);
}

struct c2_rpc_item *
itemq_iterator_next(struct itemq_iterator *it)
{
	struct c2_rpc_item *next;
	struct c2_rpc_item *result;
	struct c2_tl       *list;

	C2_PRE(it != NULL && it->ii_itemq != NULL);

	list = it->ii_curr_list;
	next = it->ii_next_item;
	if (next == NULL) {
		if (list == NULL)
			return NULL;
		/*
		 * current list is completely scanned. Move to next
		 * non-empty list.
		 */
		++list;
		while (list <= itemq_last_list(it->ii_itemq)) {
			if (!rpc_item_tlist_is_empty(list)) {
				next = rpc_item_tlist_head(list);
				it->ii_curr_list = list;
				break;
			}
			++list;
		}
		if (next == NULL) {
			C2_ASSERT(list > itemq_last_list(it->ii_itemq));
			return NULL;
		}
	}
	C2_ASSERT(next != NULL);
	result = next;
	it->ii_curr_item = result;
	it->ii_next_item = rpc_item_tlist_next(it->ii_curr_list, result);
	it->ii_nr_items_scanned++;
	return result;
}
