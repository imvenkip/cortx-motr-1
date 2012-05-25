#ifndef __COLIBRI_RPC_FORMATION2_INTERNAL_H__
#define __COLIBRI_RPC_FORMATION2_INTERNAL_H__

#include "lib/tlist.h"
#include "lib/types.h"

struct c2_rpc_frm_constraints;

struct c2_rpc_item;

enum c2_rpc_item_priority2 {
	ITEM_PRIO_MAX,
	ITEM_PRIO_MID,
	ITEM_PRIO_MIN,
	ITEM_PRIO_NR
};
struct itemq {
	uint64_t     iq_nr_items;
	c2_bcount_t  iq_ow_size;
	struct c2_tl iq_lists[ITEM_PRIO_NR];
};

void itemq_init(struct itemq *q);
void itemq_fini(struct itemq *q);
void itemq_add(struct itemq *q, struct c2_rpc_item *item);
void itemq_remove(struct c2_rpc_item *item);
c2_bcount_t itemq_compute_nr_bytes_waiting(const struct itemq *q);
uint64_t itemq_compute_nr_items(const struct itemq *q);
bool item_is_in_itemq(const struct c2_rpc_item *item,
		      const struct itemq       *q);

#define itemq_first_list(itemq) &(itemq)->iq_lists[0]
#define itemq_last_list(itemq)  \
	&(itemq)->iq_lists[ARRAY_SIZE((itemq)->iq_lists) - 1]

#define for_each_list_in_itemq(list, itemq) \
for (list = itemq_first_list(itemq);        \
     list <= itemq_last_list(itemq);        \
     list++)

struct itemq_iterator {
	uint64_t            iqt_nr_scanned;
	int32_t             iqt_curr_list;
	struct c2_rpc_item *iqt_curr_item;
	struct c2_rpc_item *iqt_next_item;
};

void itemq_iterator_init(struct itemq_iterator *it,
			 struct itemq          *q);
void itemq_iterator_fini(struct itemq_iterator *it);
struct c2_rpc_item *itemq_iterator_next(struct itemq_iterator *it);

enum c2_rpc_frm_itemq_type
item_which_queue(const struct c2_rpc_item *item);

int item_start_timer(const struct c2_rpc_item *item);
unsigned long item_timer_callback(unsigned long data);

bool constraints_are_valid(const struct c2_rpc_frm_constraints *constraints);

#define frm_first_itemq(frm) (&(frm)->f_itemq[0])
#define frm_last_itemq(frm) (&(frm)->f_itemq[ARRAY_SIZE((frm)->f_itemq) - 1])

#define for_each_itemq_in_frm(itemq, frm)  \
for (itemq = frm_first_itemq(frm); \
     itemq <= frm_last_itemq(frm); \
     itemq++)

#endif /* __COLIBRI_RPC_FORMATION2_INTERNAL_H__ */
