#ifndef __COLIBRI_RPC_SESSION_FOPS_H__
#define __COLIBRI_RPC_SESSION_FOPS_H__

#include "fop/fop.h"

struct fom;
struct c2_fom_type;

enum c2_rpc_opcodes {
	c2_rpc_conn_create_opcode = 30,
	c2_rpc_conn_terminate_opcode,
	c2_rpc_session_create_opcode,
	c2_rpc_session_destroy_opcode,
	c2_rpc_conn_create_rep_opcode,
	c2_rpc_conn_terminate_rep_opcode,
	c2_rpc_session_create_rep_opcode,
	c2_rpc_session_destroy_rep_opcode
};

extern struct c2_fop_type_ops c2_rpc_conn_create_ops;
extern struct c2_fop_type_ops c2_rpc_conn_terminate_ops;
extern struct c2_fop_type_ops c2_rpc_session_create_ops;
extern struct c2_fop_type_ops c2_rpc_session_destroy_ops;
extern struct c2_fop_type_ops c2_rpc_rep_ops;

extern struct c2_fop_type_format c2_rpc_conn_create_tfmt;
extern struct c2_fop_type_format c2_rpc_conn_create_rep_tfmt;
extern struct c2_fop_type_format c2_rpc_conn_terminate_tfmt;
extern struct c2_fop_type_format c2_rpc_conn_terminate_rep_tfmt;
extern struct c2_fop_type_format c2_rpc_session_create_tfmt;
extern struct c2_fop_type_format c2_rpc_session_create_rep_tfmt;
extern struct c2_fop_type_format c2_rpc_session_destroy_tfmt;
extern struct c2_fop_type_format c2_rpc_session_destroy_rep_tfmt;

extern struct c2_fop_type c2_rpc_conn_create_fopt;
extern struct c2_fop_type c2_rpc_conn_create_rep_fopt;
extern struct c2_fop_type c2_rpc_conn_terminate_fopt;
extern struct c2_fop_type c2_rpc_conn_terminate_rep_fopt;
extern struct c2_fop_type c2_rpc_session_create_fopt;
extern struct c2_fop_type c2_rpc_session_create_rep_fopt;
extern struct c2_fop_type c2_rpc_session_destroy_fopt;
extern struct c2_fop_type c2_rpc_session_destroy_rep_fopt;

extern int c2_rpc_conn_create_fom_init(struct c2_fop *fop,
					struct c2_fom **m);

extern int c2_rpc_session_create_fom_init(struct c2_fop *fop,
					struct c2_fom **m);

extern int c2_rpc_session_destroy_fom_init(struct c2_fop *fop,
					struct c2_fom **m);

extern int c2_rpc_conn_terminate_fom_init(struct c2_fop *fop,
					struct c2_fom **m);

extern int c2_rpc_session_fop_init(void);

extern void c2_rpc_session_fop_fini(void);

/* __COLIBRI_RPC_SESSION_FOPS_H__ */
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

