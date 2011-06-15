/* -*- C -*- */
#ifndef __COLIBRI_RPC_SESSION_FOPS_H__
#define __COLIBRI_RPC_SESSION_FOPS_H__

#include "fop/fop.h"
#include "fop/fom.h"

/**
   @addtogroup rpc_session

   @{
 */

enum c2_rpc_session_opcodes {
	C2_RPC_FOP_CONN_CREATE_OPCODE = 50,
	C2_RPC_FOP_CONN_TERMINATE_OPCODE,
	C2_RPC_FOP_SESSION_CREATE_OPCODE,
	C2_RPC_FOP_SESSION_DESTROY_OPCODE,
	C2_RPC_FOP_CONN_CREATE_REP_OPCODE,
	C2_RPC_FOP_CONN_TERMINATE_REP_OPCODE,
	C2_RPC_FOP_SESSION_CREATE_REP_OPCODE,
	C2_RPC_FOP_SESSION_DESTROY_REP_OPCODE,
	C2_RPC_FOP_NOOP
};

extern struct c2_fop_type_ops c2_rpc_fop_conn_create_ops;
extern struct c2_fop_type_ops c2_rpc_fop_conn_terminate_ops;
extern struct c2_fop_type_ops c2_rpc_fop_session_create_ops;
extern struct c2_fop_type_ops c2_rpc_fop_session_terminate_ops;
extern struct c2_fop_type_ops c2_rpc_fop_noop_ops;

extern struct c2_fop_type_format c2_rpc_fop_conn_create_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_conn_create_rep_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_conn_terminate_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_conn_terminate_rep_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_session_create_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_session_create_rep_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_session_terminate_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_session_terminate_rep_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_noop_tfmt;

extern struct c2_fop_type c2_rpc_fop_conn_create_fopt;
extern struct c2_fop_type c2_rpc_fop_conn_create_rep_fopt;
extern struct c2_fop_type c2_rpc_fop_conn_terminate_fopt;
extern struct c2_fop_type c2_rpc_fop_conn_terminate_rep_fopt;
extern struct c2_fop_type c2_rpc_fop_session_create_fopt;
extern struct c2_fop_type c2_rpc_fop_session_create_rep_fopt;
extern struct c2_fop_type c2_rpc_fop_session_terminate_fopt;
extern struct c2_fop_type c2_rpc_fop_session_terminate_rep_fopt;
extern struct c2_fop_type c2_rpc_fop_noop_fopt;

int c2_rpc_fop_conn_create_fom_init(struct c2_fop	*fop,
				    struct c2_fom 	**m);

int c2_rpc_fop_session_create_fom_init(struct c2_fop	*fop,
				       struct c2_fom 	**m);

int c2_rpc_fop_session_terminate_fom_init(struct c2_fop	*fop,
				        struct c2_fom 	**m);

int c2_rpc_fop_conn_terminate_fom_init(struct c2_fop	*fop,
				       struct c2_fom 	**m);

/*
 * No fom is defined for handling reply fops.
 * Instead each reply fop has ->fto_execute() handler defined
 */

int c2_rpc_fop_conn_create_rep_execute(struct c2_fop		*fop,
				       struct c2_fop_ctx 	*ctx);

int c2_rpc_fop_session_create_rep_execute(struct c2_fop		*fop,
				          struct c2_fop_ctx	*ctx);

int c2_rpc_fop_session_terminate_rep_execute(struct c2_fop	*fop,
				           struct c2_fop_ctx	*ctx);

int c2_rpc_fop_conn_terminate_rep_execute(struct c2_fop		*fop,
				          struct c2_fop_ctx	*ctx);

int c2_rpc_fop_noop_execute(struct c2_fop	*fop,
			    struct c2_fop_ctx	*ctx);

void c2_rpc_conn_create_reply_received(struct c2_fop *fop);

void c2_rpc_session_create_reply_received(struct c2_fop *fop);

void c2_rpc_conn_terminate_reply_received(struct c2_fop *fop);

void c2_rpc_session_terminate_reply_received(struct c2_fop *fop);

extern int c2_rpc_session_fop_init(void);

extern void c2_rpc_session_fop_fini(void);

/* __COLIBRI_RPC_SESSION_FOPS_H__ */

/** @}  End of rpc_session group */
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

