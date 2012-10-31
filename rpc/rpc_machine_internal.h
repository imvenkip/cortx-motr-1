#pragma once

#ifndef __COLIBRI_RPC_MACHINE_INT_H__
#define __COLIBRI_RPC_MACHINE_INT_H__

#include "lib/tlist.h"
#include "lib/refs.h"
#include "rpc/formation2_internal.h"

/* Imports */
struct c2_net_end_point;
struct c2_rpc_machine;

/**
   Struct c2_rpc_chan provides information about a target network endpoint.
   An rpc machine (struct c2_rpc_machine) contains list of c2_rpc_chan
   structures targeting different net endpoints.
   Rationale A physical node can have multiple endpoints associated with it.
   And multiple services can share endpoints for transport.
   The rule of thumb is to use one transfer machine per endpoint.
   So to make sure that services using same endpoint,
   use the same transfer machine, this structure has been introduced.
   Struct c2_rpc_conn is used for a particular service and now it
   points to a struct c2_rpc_chan to identify the transfer machine
   it is working with.
 */
struct c2_rpc_chan {
	/** Link in c2_rpc_machine::rm_chans list.
	    List descriptor: rpc_chan
	 */
	struct c2_tlink			  rc_linkage;
	/** Number of c2_rpc_conn structures using this transfer machine.*/
	struct c2_ref			  rc_ref;
	/** Formation state machine associated with chan. */
	struct c2_rpc_frm                 rc_frm;
	/** Destination end point to which rpcs will be sent. */
	struct c2_net_end_point		 *rc_destep;
	/** The rpc_machine, this chan structure is associated with.*/
	struct c2_rpc_machine		 *rc_rpc_machine;
	/** C2_RPC_CHAN_MAGIC */
	uint64_t			  rc_magic;
};

void c2_rpc_machine_lock(struct c2_rpc_machine *machine);
void c2_rpc_machine_unlock(struct c2_rpc_machine *machine);
bool c2_rpc_machine_is_locked(const struct c2_rpc_machine *machine);

C2_TL_DESCR_DECLARE(rpc_conn, extern);
C2_TL_DECLARE(rpc_conn, extern, struct c2_rpc_conn);

#endif /* __COLIBRI_RPC_MACHINE_INT_H__ */
