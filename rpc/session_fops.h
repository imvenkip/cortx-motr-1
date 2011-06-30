/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Rohan Puri <Rohan_Puri@xyratex.com>,
 *		    Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 04/15/2011
 */

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

extern const struct c2_fop_type_ops c2_rpc_fop_conn_create_ops;
extern const struct c2_fop_type_ops c2_rpc_fop_conn_terminate_ops;
extern const struct c2_fop_type_ops c2_rpc_fop_session_create_ops;
extern const struct c2_fop_type_ops c2_rpc_fop_session_terminate_ops;
extern const struct c2_fop_type_ops c2_rpc_fop_noop_ops;

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
				    struct c2_fom	**m);

int c2_rpc_fop_session_create_fom_init(struct c2_fop	*fop,
				       struct c2_fom	**m);

int c2_rpc_fop_session_terminate_fom_init(struct c2_fop	*fop,
					  struct c2_fom	**m);

int c2_rpc_fop_conn_terminate_fom_init(struct c2_fop	*fop,
				       struct c2_fom	**m);

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

int c2_rpc_session_fop_init(void);

void c2_rpc_session_fop_fini(void);

extern struct c2_rpc_item_type c2_rpc_item_conn_create;
extern struct c2_rpc_item_type c2_rpc_item_conn_create_rep;
extern struct c2_rpc_item_type c2_rpc_item_session_create;
extern struct c2_rpc_item_type c2_rpc_item_session_create_rep;
extern struct c2_rpc_item_type c2_rpc_item_session_terminate;
extern struct c2_rpc_item_type c2_rpc_item_session_terminate_rep;
extern struct c2_rpc_item_type c2_rpc_item_conn_terminate;
extern struct c2_rpc_item_type c2_rpc_item_conn_terminate_rep;
extern struct c2_rpc_item_type c2_rpc_item_noop;

extern const struct c2_rpc_item_ops c2_rpc_item_conn_create_ops;
extern const struct c2_rpc_item_ops c2_rpc_item_conn_terminate_ops;
extern const struct c2_rpc_item_ops c2_rpc_item_session_create_ops;
extern const struct c2_rpc_item_ops c2_rpc_item_session_terminate_ops;

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

