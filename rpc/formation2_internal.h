#ifndef __COLIBRI_RPC_FORMATION2_INTERNAL_H__
#define __COLIBRI_RPC_FORMATION2_INTERNAL_H__

#include "lib/tlist.h"
#include "lib/types.h"

struct c2_rpc_frm_constraints;
struct c2_rpc_frm;
struct c2_rpc_item;

struct c2_tl *
frm_which_queue(struct c2_rpc_frm        *frm,
		const struct c2_rpc_item *item);

void itemq_insert(struct c2_tl *q, struct c2_rpc_item *item);

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
