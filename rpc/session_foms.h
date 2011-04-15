/* -*- C -*- */

#ifndef __COLIBRI_RPC_SESSION_FOMS_H__
#define __COLIBRI_RPC_SESSION_FOMS_H__

#include "fop/fop.h"
#include "fop/fop_format.h"
#include "stob/stob.h"
#include "session_fops.h"


#ifndef __KERNEL__

int c2_rpc_dummy_req_handler(struct c2_service *s, struct c2_fop *fop,
                         void *cookie, struct c2_fol *fol,
                         struct c2_stob_domain *dom);

#endif

struct c2_fom_type* c2_rpc_fom_type_map(c2_fop_type_code_t code);

enum c2_rpc_conn_create_phases {
	FOPH_RPC_CONN_CREATE
};

enum c2_rpc_conn_terminate_phases {
	FOPH_RPC_CONN_TERMINATE
};

enum c2_rpc_session_create_phases {
	FOPH_RPC_SESSION_CREATE
};

enum c2_rpc_session_destroy_phases {
	FOPH_RPC_SESSION_DESTROY
};

struct c2_rpc_fom_ctx {
	struct c2_fom 	 rfc_fom_gen;
	struct c2_fop	*rfc_fop;
	struct c2_fop	*rfc_rep_fop;
};

int c2_rpc_fom_state(struct c2_fom *fom);


void c2_rpc_fom_fini(struct c2_fom *fom);


/* __COLIBRI_RPC_SESSION_FOMS_H__ */
#endif

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

