#include "rpc/formation2_internal.h"
#include "rpc/formation2.h"
#include "rpc/rpc2.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_FORMATION
#include "lib/trace.h"
#include "lib/misc.h"    /* C2_SET0 */

enum {
	ITEMQ_HEAD_MAGIC = 0x1111000011110000
};

C2_TL_DESCR_DEFINE(item, "rpc_item", static, struct c2_rpc_item,
		   ri_iq_link, ri_link_magic, C2_RPC_ITEM_FIELD_MAGIC,
		   ITEMQ_HEAD_MAGIC);
C2_TL_DEFINE(item, static, struct c2_rpc_item);

int c2_rpc_frm_init(struct c2_rpc_frm             *frm,
		    struct c2_rpc_machine         *rmachine,
		    struct c2_rpc_frm_constraints  constraints)
{
	struct itemq *itemq;

	C2_ENTRY("frm: %p rmachine %p", frm, rmachine);
	C2_PRE(frm != NULL &&
	       rmachine != NULL &&
	       constraints_are_valid(&constraints));

	C2_SET0(frm);
	frm->f_rmachine    = rmachine;
	frm->f_constraints = constraints; /* structure instance copy */

	for_each_itemq_in_frm(itemq, frm)
		itemq_init(itemq);

	frm->f_state = FRM_IDLE;

	C2_LEAVE("rc: 0");
	return 0;
}

void c2_rpc_frm_fini(struct c2_rpc_frm *frm)
{
	struct itemq *itemq;

	C2_ENTRY("frm: %p", frm);
	C2_PRE(frm != NULL);
	C2_LOG("frm state: %d", frm->f_state);
	C2_PRE(frm->f_state == FRM_IDLE);

	for_each_itemq_in_frm(itemq, frm)
		itemq_fini(itemq);
}

void itemq_init(struct itemq *q)
{
	struct c2_tl *list;

	C2_ENTRY("q: %p", q);

	C2_PRE(q != NULL);

	q->iq_nr_items = 0;
	q->iq_ow_size  = 0;

	for_each_list_in_itemq(list, q)
		item_tlist_init(list);

	C2_LEAVE("");
}

void itemq_fini(struct itemq *q)
{
	struct c2_tl *list;

	C2_ENTRY("q: %p", q);
	C2_PRE(q != NULL);

	C2_LOG("nr_items: %llu size: %llu",
			(unsigned long long)q->iq_nr_items,
			(unsigned long long)q->iq_ow_size);
	C2_PRE(q->iq_nr_items == 0 && q->iq_ow_size == 0);

	for_each_list_in_itemq(list, q)
		item_tlist_fini(list);

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
/*
void itemq_add(struct itemq *q, struct c2_rpc_item *item)
{
	C2_ENTRY("q: %p item: %p", q, item);
	C2_PRE(q != NULL && item != NULL);
	C2_LOG("priority: %d", (int)item->ri_prio);
	C2_PRE(item_priority_is_valid(item));
}
*/
