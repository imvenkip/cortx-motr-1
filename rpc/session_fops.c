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

#ifdef HAVE_CONFIG_H
#include <config.h>
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
#include "rpc/rpc_onwire.h"

#include "fop/fop_iterator.h"
#include "rpc/session_fops.h"
#include "rpc/session_foms.h"
#include "rpc/session.ff"
#include "rpc/session_internal.h"
#include "rpc/rpc_onwire.h" /* item_encdec() */

/**
   @addtogroup rpc_session

   @{
 */

const struct c2_fop_type_ops c2_rpc_fop_conn_establish_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_fom_init = &c2_rpc_fop_conn_establish_fom_init,
};

const struct c2_fop_type_ops c2_rpc_fop_session_establish_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_fom_init = c2_rpc_fop_session_establish_fom_init,
};

const struct c2_fop_type_ops c2_rpc_fop_session_terminate_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_fom_init = c2_rpc_fop_session_terminate_fom_init,
};

const struct c2_fop_type_ops c2_rpc_fop_conn_terminate_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_fom_init = c2_rpc_fop_conn_terminate_fom_init,
};

const struct c2_fop_type_ops c2_rpc_fop_noop_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
	.fto_execute = c2_rpc_fop_noop_execute
};

const struct c2_fop_type_ops c2_rpc_fop_conn_establish_rep_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
};
const struct c2_fop_type_ops c2_rpc_fop_conn_terminate_rep_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
};

const struct c2_fop_type_ops c2_rpc_fop_session_establish_rep_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
};

const struct c2_fop_type_ops c2_rpc_fop_session_terminate_rep_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
};


int c2_rpc_fop_conn_establish_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_rpc_fom_conn_establish *fom_ce;
	struct c2_fom                    *fom;
	struct c2_fop                    *reply_fop;

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);

	C2_ALLOC_PTR(fom_ce);
	if (fom_ce == NULL)
		return -ENOMEM;

	fop->f_type->ft_fom_type = c2_rpc_fom_conn_establish_type;

	fom = &fom_ce->fce_gen;
	fom->fo_type = &c2_rpc_fom_conn_establish_type;
	fom->fo_ops = &c2_rpc_fom_conn_establish_ops;

	fom_ce->fce_fop = fop;
	reply_fop = c2_fop_alloc(&c2_rpc_fop_conn_establish_rep_fopt, NULL);
	if (reply_fop == NULL) {
		c2_free(fom_ce);
		return -ENOMEM;
	}

	c2_rpc_item_init(&reply_fop->f_item);
	reply_fop->f_item.ri_type = reply_fop->f_type->ft_ri_type;

	fom_ce->fce_fop_rep = reply_fop;

	*m = fom;
	return 0;
}

int c2_rpc_fop_session_establish_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_rpc_fom_session_establish *fom_se;
	struct c2_fom                       *fom;
	struct c2_fop                       *reply_fop;

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);

	C2_ALLOC_PTR(fom_se);
	if (fom_se == NULL)
		return -ENOMEM;

	fop->f_type->ft_fom_type = c2_rpc_fom_session_establish_type;
	fom = &fom_se->fse_gen;
	fom->fo_type = &c2_rpc_fom_session_establish_type;
	fom->fo_ops = &c2_rpc_fom_session_establish_ops;

	fom_se->fse_fop = fop;
	reply_fop = c2_fop_alloc(&c2_rpc_fop_session_establish_rep_fopt, NULL);
	if (reply_fop == NULL) {
		c2_free(fom_se);
		return -ENOMEM;
	}

	c2_rpc_item_init(&reply_fop->f_item);
	reply_fop->f_item.ri_type = reply_fop->f_type->ft_ri_type;

	fom_se->fse_fop_rep = reply_fop;

	*m = fom;
	return 0;
}

int c2_rpc_fop_session_terminate_fom_init(struct c2_fop *fop,
					  struct c2_fom **m)
{
	struct c2_rpc_fom_session_terminate *fom_st;
	struct c2_fom                       *fom;
	struct c2_fop                       *reply_fop;

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);

	C2_ALLOC_PTR(fom_st);
	if (fom_st == NULL)
		return -ENOMEM;

	fop->f_type->ft_fom_type = c2_rpc_fom_session_terminate_type;
	fom = &fom_st->fst_gen;
	fom->fo_type = &c2_rpc_fom_session_terminate_type;
	fom->fo_ops = &c2_rpc_fom_session_terminate_ops;

	fom_st->fst_fop = fop;
	reply_fop = c2_fop_alloc(&c2_rpc_fop_session_terminate_rep_fopt, NULL);
	if (reply_fop == NULL) {
		c2_free(fom_st);
		return -ENOMEM;
	}

	c2_rpc_item_init(&reply_fop->f_item);
	reply_fop->f_item.ri_type = reply_fop->f_type->ft_ri_type;

	fom_st->fst_fop_rep = reply_fop;

	*m = fom;
	return 0;
}

int c2_rpc_fop_conn_terminate_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_rpc_fom_conn_terminate *fom_ct;
	struct c2_fom                    *fom;
	struct c2_fop                    *reply_fop;

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);

	C2_ALLOC_PTR(fom_ct);
	if (fom_ct == NULL)
		return -ENOMEM;

	fop->f_type->ft_fom_type = c2_rpc_fom_conn_terminate_type;
	fom = &fom_ct->fct_gen;
	fom->fo_type = &c2_rpc_fom_conn_terminate_type;
	fom->fo_ops = &c2_rpc_fom_conn_terminate_ops;

	fom_ct->fct_fop = fop;
	reply_fop = c2_fop_alloc(&c2_rpc_fop_conn_terminate_rep_fopt, NULL);
	if (reply_fop == NULL) {
		c2_free(fom_ct);
		return -ENOMEM;
	}

	c2_rpc_item_init(&reply_fop->f_item);
	reply_fop->f_item.ri_type = reply_fop->f_type->ft_ri_type;

	fom_ct->fct_fop_rep = reply_fop;

	*m = fom;
	return 0;
}

int c2_rpc_fop_noop_execute(struct c2_fop     *fop,
			    struct c2_fop_ctx *ctx)
{
	/* Do nothing */
	return 0;
}

/*
 *  REQUEST fops
 *  XXX C2_FOP_TYPE_DECLARE_NEW() is a temporary macro that enhances
 *  existing C2_FOP_TYPE_DECLARE() to assign item_type to the fop_type.
 */

C2_FOP_TYPE_DECLARE_NEW(c2_rpc_fop_conn_establish, "rpc_conn_establish",
			C2_RPC_FOP_CONN_ESTABLISH_OPCODE,
			&c2_rpc_fop_conn_establish_ops,
			&c2_rpc_item_conn_establish);

C2_FOP_TYPE_DECLARE_NEW(c2_rpc_fop_conn_terminate, "rpc_conn_terminate",
			C2_RPC_FOP_CONN_TERMINATE_OPCODE,
			&c2_rpc_fop_conn_terminate_ops,
			&c2_rpc_item_conn_terminate);

C2_FOP_TYPE_DECLARE_NEW(c2_rpc_fop_session_establish, "rpc_session_establish",
			C2_RPC_FOP_SESSION_ESTABLISH_OPCODE,
			&c2_rpc_fop_session_establish_ops,
			&c2_rpc_item_session_establish);

C2_FOP_TYPE_DECLARE_NEW(c2_rpc_fop_session_terminate, "rpc_session_terminate",
			C2_RPC_FOP_SESSION_TERMINATE_OPCODE,
			&c2_rpc_fop_session_terminate_ops,
			&c2_rpc_item_session_terminate);

/*
 *  REPLY fops
 */

C2_FOP_TYPE_DECLARE_NEW(c2_rpc_fop_conn_establish_rep,
			"rpc_conn_establish_reply",
			C2_RPC_FOP_CONN_ESTABLISH_REP_OPCODE,
			&c2_rpc_fop_conn_establish_rep_ops,
			&c2_rpc_item_conn_establish_rep);

C2_FOP_TYPE_DECLARE_NEW(c2_rpc_fop_conn_terminate_rep,
			"rpc_conn_terminate_reply",
			C2_RPC_FOP_CONN_TERMINATE_REP_OPCODE,
			&c2_rpc_fop_conn_terminate_rep_ops,
			&c2_rpc_item_conn_terminate_rep);

C2_FOP_TYPE_DECLARE_NEW(c2_rpc_fop_session_establish_rep,
			"rpc_session_establish_reply",
			C2_RPC_FOP_SESSION_ESTABLISH_REP_OPCODE,
			&c2_rpc_fop_session_establish_rep_ops,
			&c2_rpc_item_session_establish_rep);

C2_FOP_TYPE_DECLARE_NEW(c2_rpc_fop_session_terminate_rep,
			"rpc_session_terminate_reply",
			C2_RPC_FOP_SESSION_TERMINATE_REP_OPCODE,
			&c2_rpc_fop_session_terminate_rep_ops,
			&c2_rpc_item_session_terminate_rep);

C2_FOP_TYPE_DECLARE_NEW(c2_rpc_fop_noop, "NOOP",
			C2_RPC_FOP_NOOP, &c2_rpc_fop_noop_ops,
			&c2_rpc_item_noop);

static struct c2_fop_type *fops[] = {
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
	c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
}

int c2_rpc_session_fop_init(void)
{
	int result;

	result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
	if (result != 0)
		c2_rpc_session_fop_fini();
	return result;
}

void c2_rpc_fop_conn_establish_ctx_init(struct c2_rpc_item      *item,
					struct c2_net_end_point *ep,
					struct c2_rpcmachine    *machine)
{
	struct c2_rpc_fop_conn_establish_ctx *ctx;

	C2_PRE(item != NULL && ep != NULL && machine != NULL);

	ctx = container_of(item, struct c2_rpc_fop_conn_establish_ctx,
				cec_fop.f_item);
	C2_ASSERT(ctx != NULL);

	ctx->cec_sender_ep = ep;
	ctx->cec_rpcmachine = machine;
}
static int conn_establish_item_decode(struct c2_rpc_item_type *item_type,
				      struct c2_rpc_item     **item,
				      struct c2_bufvec_cursor *cur)
{
	struct c2_rpc_fop_conn_establish_ctx *ctx;
	struct c2_fop                        *fop;
	int                                   rc;

	C2_PRE(item_type != NULL && item != NULL && cur != NULL);
	C2_PRE(item_type == &c2_rpc_item_conn_establish);

	*item = NULL;

	C2_ALLOC_PTR(ctx);
	if (ctx == NULL)
		return -ENOMEM;

	ctx->cec_sender_ep = NULL;
	fop = &ctx->cec_fop;

	rc = c2_fop_init(fop, &c2_rpc_fop_conn_establish_fopt, NULL);
	if (rc != 0)
		goto out;

	c2_rpc_item_init(&fop->f_item);
	fop->f_item.ri_type = fop->f_type->ft_ri_type;

	rc = item_encdec(cur, &fop->f_item, C2_BUFVEC_DECODE);
	if (rc != 0)
		goto out;

	*item = &fop->f_item;
	return 0;
out:
	c2_free(ctx);
	return rc;
}

static struct c2_rpc_item_type_ops default_item_type_ops = {
	.rito_encode = c2_rpc_fop_default_encode,
	.rito_decode = c2_rpc_fop_default_decode,
        .rito_item_size = c2_rpc_item_default_size,
};

static struct c2_rpc_item_type_ops conn_establish_item_type_ops = {
	.rito_encode = c2_rpc_fop_default_encode,
	.rito_decode = conn_establish_item_decode,
        .rito_item_size = c2_rpc_item_default_size,
};

C2_RPC_ITEM_TYPE_DEF(c2_rpc_item_conn_establish,
		     C2_RPC_FOP_CONN_ESTABLISH_OPCODE,
		     C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO,
		     &conn_establish_item_type_ops);

C2_RPC_ITEM_TYPE_DEF(c2_rpc_item_conn_terminate,
		     C2_RPC_FOP_CONN_TERMINATE_OPCODE,
		     C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO,
		     &default_item_type_ops);

C2_RPC_ITEM_TYPE_DEF(c2_rpc_item_session_establish,
		     C2_RPC_FOP_SESSION_ESTABLISH_OPCODE,
		     C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO,
		     &default_item_type_ops);

C2_RPC_ITEM_TYPE_DEF(c2_rpc_item_session_terminate,
		     C2_RPC_FOP_SESSION_TERMINATE_OPCODE,
		     C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO,
		     &default_item_type_ops);

C2_RPC_ITEM_TYPE_DEF(c2_rpc_item_conn_establish_rep,
		     C2_RPC_FOP_CONN_ESTABLISH_REP_OPCODE,
		     C2_RPC_ITEM_TYPE_REPLY,
		     &default_item_type_ops);

C2_RPC_ITEM_TYPE_DEF(c2_rpc_item_conn_terminate_rep,
		     C2_RPC_FOP_CONN_TERMINATE_REP_OPCODE,
		     C2_RPC_ITEM_TYPE_REPLY,
		     &default_item_type_ops);

C2_RPC_ITEM_TYPE_DEF(c2_rpc_item_session_establish_rep,
		     C2_RPC_FOP_SESSION_ESTABLISH_REP_OPCODE,
		     C2_RPC_ITEM_TYPE_REPLY,
		     &default_item_type_ops);

C2_RPC_ITEM_TYPE_DEF(c2_rpc_item_session_terminate_rep,
		     C2_RPC_FOP_SESSION_TERMINATE_REP_OPCODE,
		     C2_RPC_ITEM_TYPE_REPLY,
		     &default_item_type_ops);

struct c2_rpc_item_type c2_rpc_item_noop = {
	.rit_ops = &default_item_type_ops,
	.rit_flags = C2_RPC_ITEM_TYPE_REQUEST
};

const struct c2_rpc_item_ops c2_rpc_item_conn_establish_ops = {
	.rio_replied = c2_rpc_conn_establish_reply_received
};

const struct c2_rpc_item_ops c2_rpc_item_conn_terminate_ops = {
	.rio_replied = c2_rpc_conn_terminate_reply_received
};

const struct c2_rpc_item_ops c2_rpc_item_session_establish_ops = {
	.rio_replied = c2_rpc_session_establish_reply_received
};

const struct c2_rpc_item_ops c2_rpc_item_session_terminate_ops = {
	.rio_replied = c2_rpc_session_terminate_reply_received
};

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

