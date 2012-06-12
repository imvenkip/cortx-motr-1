#ifndef __COLIBRI_RPC_FORMATION2_H__
#define __COLIBRI_RPC_FORMATION2_H__

#include "lib/types.h"
#include "lib/tlist.h"

struct c2_rpc_packet;
struct c2_rpc_item;
struct c2_rpc_machine;
struct c2_rpc_chan;

struct c2_rpc_frm_constraints {
	uint64_t    fc_max_nr_packets_enqed;
	uint64_t    fc_max_nr_segments;
	c2_bcount_t fc_max_packet_size;
	c2_bcount_t fc_max_nr_bytes_accumulated;
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

struct c2_rpc_frm_ops {
	bool (*fo_packet_ready)(struct c2_rpc_packet  *p,
				struct c2_rpc_machine *machine,
				struct c2_rpc_chan    *rchan);
	bool (*fo_item_bind)(struct c2_rpc_item *item);
};

struct c2_rpc_frm {
	enum frm_state                 f_state;
	struct c2_tl                   f_itemq[FRMQ_NR_QUEUES];
	uint64_t                       f_nr_items;
	c2_bcount_t                    f_nr_bytes_accumulated;
	uint64_t                       f_nr_packets_enqed;
	struct c2_rpc_frm_constraints  f_constraints;
	struct c2_rpc_machine         *f_rmachine;
	struct c2_rpc_chan            *f_rchan;
	struct c2_rpc_frm_ops         *f_ops;
};

void c2_rpc_frm_init(struct c2_rpc_frm             *frm,
		     struct c2_rpc_machine         *rmachine,
		     struct c2_rpc_chan            *rchan,
		     struct c2_rpc_frm_constraints  constraints,
		     struct c2_rpc_frm_ops         *ops);

void c2_rpc_frm_fini(struct c2_rpc_frm *frm);

void c2_rpc_frm_enq_item(struct c2_rpc_frm  *frm,
			 struct c2_rpc_item *item);

void c2_rpc_frm_packet_done(struct c2_rpc_packet *packet);

void c2_rpc_frm_run_formation(struct c2_rpc_frm *frm);

#endif /* __COLIBRI_RPC_FORMATION2_H__ */
