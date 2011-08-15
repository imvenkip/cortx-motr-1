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
#include "rpc/rpc_onwire.h"
#endif

#include "fop/fop_iterator.h"
#include "rpc/session_fops.h"
#include "rpc/session_foms.h"
#include "rpc/session.ff"
#include "rpc/session_internal.h"

/**
   @addtogroup rpc_session

   @{
 */

const struct c2_fop_type_ops c2_rpc_fop_conn_establish_ops = {
	.fto_fom_init = &c2_rpc_fop_conn_establish_fom_init,
};

const struct c2_fop_type_ops c2_rpc_fop_session_establish_ops = {
	.fto_fom_init = c2_rpc_fop_session_establish_fom_init,
};

const struct c2_fop_type_ops c2_rpc_fop_session_terminate_ops = {
	.fto_fom_init = c2_rpc_fop_session_terminate_fom_init,
};

const struct c2_fop_type_ops c2_rpc_fop_conn_terminate_ops = {
	.fto_fom_init = c2_rpc_fop_conn_terminate_fom_init,
};

const struct c2_fop_type_ops c2_rpc_fop_noop_ops = {
	.fto_execute = c2_rpc_fop_noop_execute
};

int c2_rpc_fop_conn_establish_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_rpc_fom_conn_establish *fom_ce;
	struct c2_fom                    *fom;

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
	fom_ce->fce_fop_rep = c2_fop_alloc(&c2_rpc_fop_conn_establish_rep_fopt,
						NULL);
	if (fom_ce->fce_fop_rep == NULL) {
		c2_free(fom_ce);
		return -ENOMEM;
	}

	*m = fom;
	return 0;
}

int c2_rpc_fop_session_establish_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_rpc_fom_session_establish *fom_se;
	struct c2_fom                       *fom;

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
	fom_se->fse_fop_rep = c2_fop_alloc(
				&c2_rpc_fop_session_establish_rep_fopt, NULL);
	if (fom_se->fse_fop_rep == NULL) {
		c2_free(fom_se);
		return -ENOMEM;
	}

	*m = fom;
	return 0;
}

int c2_rpc_fop_session_terminate_fom_init(struct c2_fop *fop,
					  struct c2_fom **m)
{
	struct c2_rpc_fom_session_terminate *fom_st;
	struct c2_fom                       *fom;

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
	fom_st->fst_fop_rep = c2_fop_alloc(
				&c2_rpc_fop_session_terminate_rep_fopt, NULL);
	if (fom_st->fst_fop_rep == NULL) {
		c2_free(fom_st);
		return -ENOMEM;
	}

	*m = fom;
	return 0;
}

int c2_rpc_fop_conn_terminate_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_rpc_fom_conn_terminate *fom_ct;
	struct c2_fom                    *fom;

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
	fom_ct->fct_fop_rep = c2_fop_alloc(&c2_rpc_fop_conn_terminate_rep_fopt,
						NULL);
	if (fom_ct->fct_fop_rep == NULL) {
		c2_free(fom_ct);
		return -ENOMEM;
	}

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
			NULL,
			&c2_rpc_item_conn_establish_rep);

C2_FOP_TYPE_DECLARE_NEW(c2_rpc_fop_conn_terminate_rep,
			"rpc_conn_terminate_reply",
			C2_RPC_FOP_CONN_TERMINATE_REP_OPCODE,
			NULL,
			&c2_rpc_item_conn_terminate_rep);

C2_FOP_TYPE_DECLARE_NEW(c2_rpc_fop_session_establish_rep,
			"rpc_session_establish_reply",
			C2_RPC_FOP_SESSION_ESTABLISH_REP_OPCODE,
			NULL,
			&c2_rpc_item_session_establish_rep);

C2_FOP_TYPE_DECLARE_NEW(c2_rpc_fop_session_terminate_rep,
			"rpc_session_terminate_reply",
			C2_RPC_FOP_SESSION_TERMINATE_REP_OPCODE,
			NULL,
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

static bool default_is_io_req(struct c2_rpc_item *item)
{
	return false;
}

static struct c2_rpc_item_type_ops default_item_type_ops = {
	.rito_is_io_req = default_is_io_req,
	.rito_encode = c2_rpc_fop_default_encode,
	.rito_decode = c2_rpc_fop_default_decode,
        .rito_item_size = c2_rpc_item_default_size,
};

struct c2_rpc_item_type c2_rpc_item_conn_establish = {
	.rit_ops = &default_item_type_ops,
	.rit_item_is_req = true,
	.rit_mutabo = true,
};

struct c2_rpc_item_type c2_rpc_item_conn_terminate = {
	.rit_ops = &default_item_type_ops,
	.rit_item_is_req = true,
	.rit_mutabo = true,
};

struct c2_rpc_item_type c2_rpc_item_session_establish = {
	.rit_ops = &default_item_type_ops,
	.rit_item_is_req = true,
	.rit_mutabo = true,
};

struct c2_rpc_item_type c2_rpc_item_session_terminate = {
	.rit_ops = &default_item_type_ops,
	.rit_item_is_req = true,
	.rit_mutabo = true,
};

struct c2_rpc_item_type c2_rpc_item_conn_establish_rep = {
	.rit_ops = &default_item_type_ops,
	.rit_item_is_req = false,
	.rit_mutabo = false
};

struct c2_rpc_item_type c2_rpc_item_conn_terminate_rep = {
	.rit_ops = &default_item_type_ops,
	.rit_item_is_req = false,
	.rit_mutabo = false
};

struct c2_rpc_item_type c2_rpc_item_session_establish_rep = {
	.rit_ops = &default_item_type_ops,
	.rit_item_is_req = false,
	.rit_mutabo = false
};

struct c2_rpc_item_type c2_rpc_item_session_terminate_rep = {
	.rit_ops = &default_item_type_ops,
	.rit_item_is_req = false,
	.rit_mutabo = false
};

struct c2_rpc_item_type c2_rpc_item_noop = {
	.rit_ops = &default_item_type_ops,
	.rit_item_is_req = true,
	.rit_mutabo = false
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

