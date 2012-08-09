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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "fop/fom.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"

#ifdef __KERNEL__
#include "rpc/session_k.h"
#else
#include "rpc/session_u.h"
#endif
#include "fop/fop_item_type.h"

#include "fop/fop_iterator.h"
#include "rpc/session_fops.h"
#include "rpc/session_foms.h"
#include "rpc/session.ff"
#include "rpc/session_internal.h"
#include "rpc/rpc_onwire.h" /* item_encdec() */

/**
   @addtogroup rpc_session

   @{

   This file contains definitions of fop types and rpc item types, of fops
   belonging to rpc-session module
 */

int c2_rpc_fop_noop_execute(struct c2_fop     *fop,
			    struct c2_fop_ctx *ctx)
{
	/* Do nothing */
	return 0;
}

static void conn_establish_item_free(struct c2_rpc_item *item)
{
	struct c2_rpc_fop_conn_establish_ctx *ctx;
	struct c2_fop                        *fop;

	fop = c2_rpc_item_to_fop(item);
	ctx = container_of(fop, struct c2_rpc_fop_conn_establish_ctx, cec_fop);
	c2_free(ctx);
}

static const struct c2_rpc_item_ops rcv_conn_establish_item_ops = {
	.rio_free = conn_establish_item_free,
};

static int conn_establish_item_decode(struct c2_rpc_item_type *item_type,
				      struct c2_rpc_item     **item,
				      struct c2_bufvec_cursor *cur)
{
	struct c2_rpc_fop_conn_establish_ctx *ctx;
	struct c2_fop                        *fop;
	int                                   rc;

	C2_PRE(item_type != NULL && item != NULL && cur != NULL);
	C2_PRE(item_type->rit_opcode == C2_RPC_CONN_ESTABLISH_OPCODE);

	*item = NULL;

	C2_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return -ENOMEM;

	ctx->cec_sender_ep = NULL;
	fop = &ctx->cec_fop;

	rc = c2_fop_init(fop, &c2_rpc_fop_conn_establish_fopt, NULL);
	if (rc != 0)
		goto out;

	rc = item_encdec(cur, &fop->f_item, C2_BUFVEC_DECODE);
	if (rc != 0)
		goto out;

	*item           = &fop->f_item;
	(*item)->ri_ops = &rcv_conn_establish_item_ops;

	return 0;
out:
	c2_free(ctx);
	return rc;
}

static const struct c2_fop_type_ops default_fop_type_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
};

static const struct c2_fop_type_ops default_reply_fop_type_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
};

const struct c2_fop_type_ops c2_rpc_fop_noop_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_execute = c2_rpc_fop_noop_execute
};


static struct c2_rpc_item_type_ops conn_establish_item_type_ops = {
	.rito_encode = c2_fop_item_type_default_encode,
	.rito_decode = conn_establish_item_decode,
        .rito_item_size = c2_fop_item_type_default_onwire_size,
};

/*
 *  REQUEST fops
 */

C2_FOP_TYPE_DECLARE_OPS(c2_rpc_fop_conn_establish, "rpc_conn_establish",
			&default_fop_type_ops,
			C2_RPC_CONN_ESTABLISH_OPCODE,
			C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO,
			&conn_establish_item_type_ops);

C2_FOP_TYPE_DECLARE(c2_rpc_fop_conn_terminate, "rpc_conn_terminate",
		    &default_fop_type_ops,
		    C2_RPC_CONN_TERMINATE_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO);

C2_FOP_TYPE_DECLARE(c2_rpc_fop_session_establish, "rpc_session_establish",
		    &default_fop_type_ops,
		    C2_RPC_SESSION_ESTABLISH_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO);

C2_FOP_TYPE_DECLARE(c2_rpc_fop_session_terminate, "rpc_session_terminate",
		    &default_fop_type_ops,
		    C2_RPC_SESSION_TERMINATE_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO);

/*
 *  REPLY fops
 */

C2_FOP_TYPE_DECLARE(c2_rpc_fop_conn_establish_rep, "rpc_conn_establish_reply",
		    &default_reply_fop_type_ops,
		    C2_RPC_CONN_ESTABLISH_REP_OPCODE,
		    C2_RPC_ITEM_TYPE_REPLY);

C2_FOP_TYPE_DECLARE(c2_rpc_fop_conn_terminate_rep, "rpc_conn_terminate_reply",
		    &default_reply_fop_type_ops,
		    C2_RPC_CONN_TERMINATE_REP_OPCODE,
		    C2_RPC_ITEM_TYPE_REPLY);

C2_FOP_TYPE_DECLARE(c2_rpc_fop_session_establish_rep,
		    "rpc_session_establish_reply",
		    &default_reply_fop_type_ops,
		    C2_RPC_SESSION_ESTABLISH_REP_OPCODE,
		    C2_RPC_ITEM_TYPE_REPLY);

C2_FOP_TYPE_DECLARE(c2_rpc_fop_session_terminate_rep,
		    "rpc_session_terminate_reply",
		    &default_reply_fop_type_ops,
		    C2_RPC_SESSION_TERMINATE_REP_OPCODE,
		    C2_RPC_ITEM_TYPE_REPLY);

C2_FOP_TYPE_DECLARE(c2_rpc_fop_noop, "NOOP",
		    &c2_rpc_fop_noop_ops,
		    C2_RPC_NOOP_OPCODE,
		    C2_RPC_ITEM_TYPE_REQUEST);

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
	c2_fop_type_fini_nr(fop_types, ARRAY_SIZE(fop_types));
}

int c2_rpc_session_fop_init(void)
{
	int result;

	result = c2_fop_type_build_nr(fop_types, ARRAY_SIZE(fop_types));
	if (result != 0)
		c2_rpc_session_fop_fini();

	c2_fom_type_register(&c2_rpc_fom_conn_establish_type);
	c2_fom_type_register(&c2_rpc_fom_session_establish_type);
	c2_fom_type_register(&c2_rpc_fom_conn_terminate_type);
	c2_fom_type_register(&c2_rpc_fom_session_terminate_type);
	c2_rpc_fop_conn_establish_fopt.ft_fom_type =
		c2_rpc_fom_conn_establish_type;

	c2_rpc_fop_conn_terminate_fopt.ft_fom_type =
		c2_rpc_fom_conn_terminate_type;

	c2_rpc_fop_session_establish_fopt.ft_fom_type =
		c2_rpc_fom_session_establish_type;

	c2_rpc_fop_session_terminate_fopt.ft_fom_type =
		c2_rpc_fom_session_terminate_type;

	return result;
}

void c2_rpc_fop_conn_establish_ctx_init(struct c2_rpc_item      *item,
					struct c2_net_end_point *ep,
					struct c2_rpc_machine   *machine)
{
	struct c2_rpc_fop_conn_establish_ctx *ctx;

	C2_PRE(item != NULL && ep != NULL && machine != NULL);

	ctx = container_of(item, struct c2_rpc_fop_conn_establish_ctx,
				cec_fop.f_item);
	C2_ASSERT(ctx != NULL);

	c2_net_end_point_get(ep);
	ctx->cec_sender_ep = ep;
	ctx->cec_rpc_machine = machine;
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

