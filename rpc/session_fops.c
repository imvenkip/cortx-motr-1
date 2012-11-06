/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "net/net.h"        /* c2_net_end_point_get */
#include "fop/fom.h"
#include "fop/fop.h"
#include "fop/fop_item_type.h"
#include "dtm/verno_xc.h" /* c2_xc_verno_init */

#include "rpc/rpc.h"
#include "rpc/rpc_internal.h"

/**
   @addtogroup rpc_session

   @{

   This file contains definitions of fop types and rpc item types, of fops
   belonging to rpc-session module
 */

static void conn_establish_item_free(struct c2_rpc_item *item)
{
	struct c2_rpc_fop_conn_establish_ctx *ctx;
	struct c2_fop                        *fop;

	fop = c2_rpc_item_to_fop(item);
	c2_fop_fini(fop);
	ctx = container_of(fop, struct c2_rpc_fop_conn_establish_ctx, cec_fop);
	c2_free(ctx);
}

static const struct c2_rpc_item_ops rcv_conn_establish_item_ops = {
	.rio_free = conn_establish_item_free,
};

static int conn_establish_item_decode(const struct c2_rpc_item_type *item_type,
				      struct c2_rpc_item           **item,
				      struct c2_bufvec_cursor       *cur)
{
	struct c2_rpc_fop_conn_establish_ctx *ctx;
	struct c2_fop                        *fop;
	int                                   rc;

	C2_ENTRY("item_opcode: %u", item_type->rit_opcode);
	C2_PRE(item_type != NULL && item != NULL && cur != NULL);
	C2_PRE(item_type->rit_opcode == C2_RPC_CONN_ESTABLISH_OPCODE);

	*item = NULL;

	C2_ALLOC_PTR(ctx);
	if (ctx == NULL)
		C2_RETURN(-ENOMEM);

	ctx->cec_sender_ep = NULL;
	fop         = &ctx->cec_fop;

	/**
	   No need to allocate fop->f_data.fd_data since xcode allocates
	   top level object also.
	 */
	c2_fop_init(fop, &c2_rpc_fop_conn_establish_fopt, NULL);

	rc = c2_fop_item_encdec(&fop->f_item, cur, C2_BUFVEC_DECODE);
	if (rc != 0)
		goto out;

	*item           = &fop->f_item;
	(*item)->ri_ops = &rcv_conn_establish_item_ops;

	C2_RETURN(0);
out:
	c2_free(ctx);
	C2_RETURN(rc);
}

const struct c2_fop_type_ops c2_rpc_fop_noop_ops = {
};

static struct c2_rpc_item_type_ops conn_establish_item_type_ops = {
	.rito_encode       = c2_fop_item_type_default_encode,
	.rito_decode       = conn_establish_item_decode,
        .rito_payload_size = c2_fop_item_type_default_payload_size,
};

struct c2_fop_type c2_rpc_fop_conn_establish_fopt;
struct c2_fop_type c2_rpc_fop_conn_establish_rep_fopt;
struct c2_fop_type c2_rpc_fop_conn_terminate_fopt;
struct c2_fop_type c2_rpc_fop_conn_terminate_rep_fopt;
struct c2_fop_type c2_rpc_fop_session_establish_fopt;
struct c2_fop_type c2_rpc_fop_session_establish_rep_fopt;
struct c2_fop_type c2_rpc_fop_session_terminate_fopt;
struct c2_fop_type c2_rpc_fop_session_terminate_rep_fopt;
struct c2_fop_type c2_rpc_fop_noop_fopt;

static struct c2_fop_type *fop_types[] = {
	&c2_rpc_fop_conn_establish_fopt,
	&c2_rpc_fop_conn_terminate_fopt,
	&c2_rpc_fop_session_establish_fopt,
	&c2_rpc_fop_session_terminate_fopt,
	&c2_rpc_fop_conn_establish_rep_fopt,
	&c2_rpc_fop_conn_terminate_rep_fopt,
	&c2_rpc_fop_session_establish_rep_fopt,
	&c2_rpc_fop_session_terminate_rep_fopt,
	&c2_rpc_fop_noop_fopt,
};

void c2_rpc_session_fop_fini(void)
{
	c2_fop_type_fini(&c2_rpc_fop_noop_fopt);
	c2_fop_type_fini(&c2_rpc_fop_session_terminate_rep_fopt);
	c2_fop_type_fini(&c2_rpc_fop_session_establish_rep_fopt);
	c2_fop_type_fini(&c2_rpc_fop_conn_terminate_rep_fopt);
	c2_fop_type_fini(&c2_rpc_fop_conn_establish_rep_fopt);
	c2_fop_type_fini(&c2_rpc_fop_session_terminate_fopt);
	c2_fop_type_fini(&c2_rpc_fop_session_establish_fopt);
	c2_fop_type_fini(&c2_rpc_fop_conn_terminate_fopt);
	c2_fop_type_fini(&c2_rpc_fop_conn_establish_fopt);
	c2_xc_session_fini();
	c2_xc_rpc_onwire_fini();
	c2_xc_verno_fini();
}

extern struct c2_fom_type_ops c2_rpc_fom_conn_establish_type_ops;
extern struct c2_fom_type_ops c2_rpc_fom_session_establish_type_ops;
extern struct c2_fom_type_ops c2_rpc_fom_conn_terminate_type_ops;
extern struct c2_fom_type_ops c2_rpc_fom_session_terminate_type_ops;

int c2_rpc_session_fop_init(void)
{
	/**
	 * @todo This should be done from dtm subsystem init.
	 */
	c2_xc_verno_init();
	c2_xc_rpc_onwire_init();
	c2_xc_session_init();
	return  C2_FOP_TYPE_INIT(&c2_rpc_fop_conn_establish_fopt,
			 .name      = "Rpc conn establish",
			 .opcode    = C2_RPC_CONN_ESTABLISH_OPCODE,
			 .xt        = c2_rpc_fop_conn_establish_xc,
			 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
				      C2_RPC_ITEM_TYPE_MUTABO,
			 .rpc_ops   = &conn_establish_item_type_ops,
			 .fom_ops   = &c2_rpc_fom_conn_establish_type_ops,
			 .sm        = &c2_generic_conf) ?:
		C2_FOP_TYPE_INIT(&c2_rpc_fop_conn_terminate_fopt,
			 .name      = "Rpc conn terminate",
			 .opcode    = C2_RPC_CONN_TERMINATE_OPCODE,
			 .xt        = c2_rpc_fop_conn_terminate_xc,
			 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
				      C2_RPC_ITEM_TYPE_MUTABO,
			 .fom_ops   = &c2_rpc_fom_conn_terminate_type_ops,
			 .sm        = &c2_generic_conf) ?:
		C2_FOP_TYPE_INIT(&c2_rpc_fop_session_establish_fopt,
			 .name      = "Rpc session establish",
			 .opcode    = C2_RPC_SESSION_ESTABLISH_OPCODE,
			 .xt        = c2_rpc_fop_session_establish_xc,
			 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
				      C2_RPC_ITEM_TYPE_MUTABO,
			 .fom_ops   = &c2_rpc_fom_session_establish_type_ops,
			 .sm        = &c2_generic_conf) ?:
		C2_FOP_TYPE_INIT(&c2_rpc_fop_session_terminate_fopt,
			 .name      = "Rpc session terminate",
			 .opcode    = C2_RPC_SESSION_TERMINATE_OPCODE,
			 .xt        = c2_rpc_fop_session_terminate_xc,
			 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST |
				      C2_RPC_ITEM_TYPE_MUTABO,
			 .fom_ops   = &c2_rpc_fom_session_terminate_type_ops,
			 .sm        = &c2_generic_conf) ?:
		C2_FOP_TYPE_INIT(&c2_rpc_fop_conn_establish_rep_fopt,
			 .name      = "Rpc conn establish reply",
			 .opcode    = C2_RPC_CONN_ESTABLISH_REP_OPCODE,
			 .xt        = c2_rpc_fop_conn_establish_rep_xc,
			 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
		C2_FOP_TYPE_INIT(&c2_rpc_fop_conn_terminate_rep_fopt,
			 .name      = "Rpc conn terminate reply",
			 .opcode    = C2_RPC_CONN_TERMINATE_REP_OPCODE,
			 .xt        = c2_rpc_fop_conn_terminate_rep_xc,
			 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
		C2_FOP_TYPE_INIT(&c2_rpc_fop_session_establish_rep_fopt,
			 .name      = "Rpc session establish reply",
			 .opcode    = C2_RPC_SESSION_ESTABLISH_REP_OPCODE,
			 .xt        = c2_rpc_fop_session_establish_rep_xc,
			 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
		C2_FOP_TYPE_INIT(&c2_rpc_fop_session_terminate_rep_fopt,
			 .name      = "Rpc session terminate reply",
			 .opcode    = C2_RPC_SESSION_TERMINATE_REP_OPCODE,
			 .xt        = c2_rpc_fop_session_terminate_rep_xc,
			 .rpc_flags = C2_RPC_ITEM_TYPE_REPLY) ?:
		C2_FOP_TYPE_INIT(&c2_rpc_fop_noop_fopt,
			 .name      = "No-op",
			 .opcode    = C2_RPC_NOOP_OPCODE,
			 .xt        = c2_rpc_fop_noop_xc,
			 .rpc_flags = C2_RPC_ITEM_TYPE_REQUEST,
			 .fop_ops   = &c2_rpc_fop_noop_ops);
}

void c2_rpc_fop_conn_establish_ctx_init(struct c2_rpc_item      *item,
					struct c2_net_end_point *ep,
					struct c2_rpc_machine   *machine)
{
	struct c2_rpc_fop_conn_establish_ctx *ctx;

	C2_ENTRY("item: %p, ep_addr: %s, machine: %p", item,
		 (char *)ep->nep_addr, machine);
	C2_PRE(item != NULL && ep != NULL && machine != NULL);

	ctx = container_of(item, struct c2_rpc_fop_conn_establish_ctx,
				cec_fop.f_item);
	C2_ASSERT(ctx != NULL);

	c2_net_end_point_get(ep);
	ctx->cec_sender_ep = ep;
	ctx->cec_rpc_machine = machine;
	C2_LEAVE();
}

bool c2_rpc_item_is_control_msg(const struct c2_rpc_item *item)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fop_types); i++) {
		if (item->ri_type->rit_opcode ==
		    fop_types[i]->ft_rpc_item_type.rit_opcode)
			return true;
	}
	return false;
}

/** @} End of rpc_session group */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
