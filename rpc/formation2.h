#ifndef __COLIBRI_RPC_FORMATION2_H__
#define __COLIBRI_RPC_FORMATION2_H__

#include "lib/types.h"
#include "rpc/formation2_internal.h"

struct c2_rpc_packet;
struct c2_rpc_slot;

struct c2_rpc_frm_constraints {
	uint64_t    f_max_nr_packets_enqed;
	uint64_t    f_max_nr_segments;
	c2_bcount_t f_max_packet_size;
	c2_bcount_t f_max_nr_bytes_accumulated;
};

void
c2_rpc_frm_constraints_get_defaults(struct c2_rpc_frm_constraints *constraint);

enum frm_state {
	FRM_UNINITIALISED,
	FRM_IDLE,
	FRM_BUSY,
	FRM_NR_STATES
};

enum c2_rpc_frm_itemq_type {
	FRMQ_TIMEDOUT_BOUND,
	FRMQ_TIMEDOUT_UNBOUND,
	FRMQ_TIMEDOUT_ONE_WAY,
	FRMQ_WAITING_BOUND,
	FRMQ_WAITING_UNBOUND,
	FRMQ_WAITING_ONE_WAY,
	FRMQ_NR_QUEUES
};

struct c2_rpc_frm {
	enum frm_state                 f_state;
	struct itemq                   f_itemq[FRMQ_NR_QUEUES];
	uint64_t                       f_nr_items;
	size_t                         f_nr_bytes_accumulated;
	uint64_t                       f_nr_packets_enqed;
	struct c2_rpc_frm_constraints  f_constraints;
	struct c2_rpc_machine         *f_rmachine;
};

int c2_rpc_frm_init(struct c2_rpc_frm             *frm,
		    struct c2_rpc_machine         *rmachine,
		    struct c2_rpc_frm_constraints  constraints);

void c2_rpc_frm_fini(struct c2_rpc_frm *frm);

void c2_rpc_frm_enq_item(struct c2_rpc_frm  *frm,
			 struct c2_rpc_item *item);

void c2_rpc_frm_slot_ready(struct c2_rpc_frm  *frm,
			   struct c2_rpc_slot *slot);
#endif /* __COLIBRI_RPC_FORMATION2_H__ */
