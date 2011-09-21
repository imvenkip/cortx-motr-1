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
 *                  Amit Jambure <Amit_Jambure@xyratex.com>
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
	C2_RPC_FOP_CONN_ESTABLISH_OPCODE = 50,
	C2_RPC_FOP_CONN_TERMINATE_OPCODE,
	C2_RPC_FOP_SESSION_ESTABLISH_OPCODE,
	C2_RPC_FOP_SESSION_TERMINATE_OPCODE,
	C2_RPC_FOP_CONN_ESTABLISH_REP_OPCODE,
	C2_RPC_FOP_CONN_TERMINATE_REP_OPCODE,
	C2_RPC_FOP_SESSION_ESTABLISH_REP_OPCODE,
	C2_RPC_FOP_SESSION_TERMINATE_REP_OPCODE,
	C2_RPC_FOP_NOOP
};

extern const struct c2_fop_type_ops c2_rpc_fop_conn_establish_ops;
extern const struct c2_fop_type_ops c2_rpc_fop_conn_terminate_ops;
extern const struct c2_fop_type_ops c2_rpc_fop_session_establish_ops;
extern const struct c2_fop_type_ops c2_rpc_fop_session_terminate_ops;
extern const struct c2_fop_type_ops c2_rpc_fop_noop_ops;

extern struct c2_fop_type_format c2_rpc_fop_conn_establish_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_conn_establish_rep_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_conn_terminate_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_conn_terminate_rep_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_session_establish_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_session_establish_rep_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_session_terminate_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_session_terminate_rep_tfmt;
extern struct c2_fop_type_format c2_rpc_fop_noop_tfmt;

extern struct c2_fop_type c2_rpc_fop_conn_establish_fopt;
extern struct c2_fop_type c2_rpc_fop_conn_establish_rep_fopt;
extern struct c2_fop_type c2_rpc_fop_conn_terminate_fopt;
extern struct c2_fop_type c2_rpc_fop_conn_terminate_rep_fopt;
extern struct c2_fop_type c2_rpc_fop_session_establish_fopt;
extern struct c2_fop_type c2_rpc_fop_session_establish_rep_fopt;
extern struct c2_fop_type c2_rpc_fop_session_terminate_fopt;
extern struct c2_fop_type c2_rpc_fop_session_terminate_rep_fopt;
extern struct c2_fop_type c2_rpc_fop_noop_fopt;

int c2_rpc_fop_conn_establish_fom_init(struct c2_fop  *fop,
				       struct c2_fom **m);

int c2_rpc_fop_session_establish_fom_init(struct c2_fop  *fop,
					  struct c2_fom **m);

int c2_rpc_fop_session_terminate_fom_init(struct c2_fop  *fop,
					  struct c2_fom **m);

int c2_rpc_fop_conn_terminate_fom_init(struct c2_fop  *fop,
				       struct c2_fom **m);

int c2_rpc_fop_noop_execute(struct c2_fop     *fop,
			    struct c2_fop_ctx *ctx);

int c2_rpc_session_fop_init(void);

void c2_rpc_session_fop_fini(void);

extern struct c2_rpc_item_type c2_rpc_item_conn_establish;
extern struct c2_rpc_item_type c2_rpc_item_conn_establish_rep;
extern struct c2_rpc_item_type c2_rpc_item_session_establish;
extern struct c2_rpc_item_type c2_rpc_item_session_establish_rep;
extern struct c2_rpc_item_type c2_rpc_item_session_terminate;
extern struct c2_rpc_item_type c2_rpc_item_session_terminate_rep;
extern struct c2_rpc_item_type c2_rpc_item_conn_terminate;
extern struct c2_rpc_item_type c2_rpc_item_conn_terminate_rep;
extern struct c2_rpc_item_type c2_rpc_item_noop;

extern const struct c2_rpc_item_ops c2_rpc_item_conn_establish_ops;
extern const struct c2_rpc_item_ops c2_rpc_item_conn_terminate_ops;
extern const struct c2_rpc_item_ops c2_rpc_item_session_establish_ops;
extern const struct c2_rpc_item_ops c2_rpc_item_session_terminate_ops;

/**
   Container for CONN_ESTABLISH fop.

   This is required only on receiver side so that,
   c2_rpc_fom_conn_establish_state() can find out sender's endpoint, while
   initialising receiver side c2_rpc_conn object.

   Just before calling c2_rpc_item_received(), rpc_net_buf_received(), sets
   cec_sender_ep, by using c2_net_buffer::nb_ep attribute.
 */
struct c2_rpc_fop_conn_establish_ctx
{
	/** fop instance of type c2_rpc_fop_conn_establish_fopt */
	struct c2_fop            cec_fop;

	/** end point of sender, who has sent the conn_establish request fop */
	struct c2_net_end_point *cec_sender_ep;

	/** New rpc connection needs to be established in context of this
	    rpcmachine */
	struct c2_rpcmachine    *cec_rpcmachine;
};

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

