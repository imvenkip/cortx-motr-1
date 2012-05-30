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
void frm_itemq_remove(struct c2_rpc_frm *frm, struct c2_rpc_item *item);
void frm_balance(struct c2_rpc_frm *frm);
void frm_fill_packet(struct c2_rpc_frm *frm, struct c2_rpc_packet *p);
void frm_packet_ready(struct c2_rpc_frm *frm, struct c2_rpc_packet *p);
int item_start_timer(const struct c2_rpc_item *item);
unsigned long item_timer_callback(unsigned long data);

bool constraints_are_valid(const struct c2_rpc_frm_constraints *constraints);

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
	/* XXX Temporary */
	c->fc_max_nr_packets_enqed     = 1000;
	c->fc_max_nr_segments          = 128;
	c->fc_max_packet_size          = 100;
	c->fc_max_nr_bytes_accumulated = 100;
}

int c2_rpc_frm_init(struct c2_rpc_frm             *frm,
		    struct c2_rpc_machine         *rmachine,
		    struct c2_rpc_frm_constraints  constraints,
		    struct c2_rpc_frm_ops         *ops)
{
	struct c2_tl *q;

	C2_ENTRY("frm: %p rmachine %p", frm, rmachine);
	C2_PRE(frm != NULL &&
	       rmachine != NULL &&
	       ops != NULL &&
	       constraints_are_valid(&constraints));

	C2_SET0(frm);
	frm->f_rmachine    = rmachine;
	frm->f_ops         = ops;
	frm->f_constraints = constraints; /* structure instance copy */

	for_each_itemq_in_frm(q, frm)
		itemq_tlist_init(q);

	frm->f_state = FRM_IDLE;

	C2_POST(frm_invariant(frm) && frm->f_state == FRM_IDLE);
	C2_LEAVE("rc: 0");
	return 0;
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
	C2_LEAVE("qtype: %s", str_qtype(qtype));
	return &frm->f_itemq[qtype];
}

/** XXX @todo This should be part of c2_rpc_item invariant */
bool item_priority_is_valid(const struct c2_rpc_item *item)
{
	return item->ri_prio >= C2_RPC_ITEM_PRIO_MIN &&
	       item->ri_prio <= C2_RPC_ITEM_PRIO_MAX;
}

bool constraints_are_valid(const struct c2_rpc_frm_constraints *constraints)
{
	return true;
}

void frm_itemq_insert(struct c2_rpc_frm *frm, struct c2_rpc_item *new_item)
{
	struct c2_rpc_item *item;
	struct c2_tl       *q;

	C2_ENTRY("frm: %p item: %p", frm, new_item);
	C2_PRE(new_item != NULL);
	C2_LOG("priority: %d deadline: [%llu:%llu]",
			(int)new_item->ri_prio,
			(ULL)c2_time_seconds(new_item->ri_deadline),
			(ULL)c2_time_nanoseconds(new_item->ri_deadline));

	C2_PRE(item_priority_is_valid(new_item));

	q = frm_which_queue(frm, new_item);

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

	if (item == NULL) {
		itemq_tlink_init_at_tail(new_item, q);
	} else {
		C2_ASSERT(item->ri_prio < new_item->ri_prio ||
			  (item->ri_prio == new_item->ri_prio &&
			   c2_time_after(item->ri_deadline,
					 new_item->ri_deadline)));
		itemq_tlist_add_before(item, new_item);
	}

	new_item->ri_itemq = q;
	C2_CNT_INC(frm->f_nr_items);
	frm->f_nr_bytes_accumulated += c2_rpc_item_size(new_item);

	C2_LEAVE();
}

void frm_itemq_remove(struct c2_rpc_frm *frm, struct c2_rpc_item *item)
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
	int                   count = 0;

	C2_ENTRY("frm: %p", frm);
	C2_PRE(frm != NULL);
	C2_LOG("ready: %s", frm_is_ready(frm) ? "true" : "false");

	while (frm_is_ready(frm)) {
		C2_ALLOC_PTR(p);
		if (p == NULL) {
			C2_LOG("Error: packet allocation failed");
			C2_LEAVE("%d packets formed", count);
			return;
		}
		c2_rpc_packet_init(p);
		frm_fill_packet(frm, p);
		frm_packet_ready(frm, p);
		++count;
	}

	C2_LEAVE("%d packet(s) formed", count);
}

void frm_packet_ready(struct c2_rpc_frm *frm, struct c2_rpc_packet *p)
{
	C2_ENTRY("frm: %p packet %p", frm, p);

	C2_PRE(frm != NULL && p != NULL && !c2_rpc_packet_is_empty(p));
	C2_PRE(frm->f_ops != NULL && frm->f_ops->fo_packet_ready != NULL);
	C2_LOG("nr_items: %llu", (ULL)p->rp_nr_items);

	++frm->f_nr_packets_enqed;
	frm->f_ops->fo_packet_ready(p);
}

void frm_fill_packet(struct c2_rpc_frm *frm, struct c2_rpc_packet *p)
{
	enum c2_rpc_frm_itemq_type  qtype;
	struct c2_rpc_item         *item;
	struct c2_tl               *q;

	auto bool item_will_exceed_packet_size(void);

	C2_ENTRY("frm: %p packet: %p", frm, p);

	for (qtype = FRMQ_TIMEDOUT_BOUND; qtype < FRMQ_NR_QUEUES; ++qtype) {
		q = &frm->f_itemq[qtype];
		c2_tl_for(itemq, q, item) {
			if (item_will_exceed_packet_size())
				goto out;
			frm_itemq_remove(frm, item);
			c2_rpc_packet_add_item(p, item);
		} c2_tl_endfor;
	}

out:
	C2_ASSERT(frm_invariant(frm));
	C2_LEAVE();

	bool item_will_exceed_packet_size(void) {
		return p->rp_size + c2_rpc_item_size(item) >
			frm->f_constraints.fc_max_packet_size;
	}
}
