#ifndef _C2_RPC_SESSION_FOM_H
#define _C2_RPC_SESSION_FOM_H


#include "fop/fop.h"
#include "fop/fop_format.h"

#include "rpc/session_fops.h"
#ifndef __KERNEL__
#include "rpc/session_u.h"
#endif

#include "fop/fom.h"

enum c2_rpc_fom_conn_create_phase {
	FOPH_CONN_CREATE
};

struct c2_rpc_fom_conn_create {
	struct c2_fom		fcc_gen;
	struct c2_fop		*fcc_fop;
	struct c2_fop		*fcc_fop_rep;
	struct c2_dbenv		*fcc_dbenv;
	struct c2_table		*fcc_slot_table;
	struct c2_db_tx		fcc_tx;
};
extern struct c2_fom_type c2_rpc_fom_conn_create_type;
extern struct c2_fom_ops c2_rpc_fom_conn_create_ops;

extern int c2_rpc_fom_conn_create_state(struct c2_fom *);
extern void c2_rpc_fom_conn_create_fini(struct c2_fom *);
#endif
