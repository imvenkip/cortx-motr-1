/* -*- C -*- */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "fop/fom.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"
#include "rpc/session_u.h"
#include "fop/fop_iterator.h"
#include "rpc/session_fops.h"
#include "rpc/session_foms.h"
#include "rpc/session.ff"
#include "rpc/session_int.h"

/**
   @addtogroup rpc_session

   @{
 */

struct c2_fop_type_ops c2_rpc_fop_conn_create_ops = {
	.fto_fom_init = &c2_rpc_fop_conn_create_fom_init,
};

struct c2_fop_type_ops c2_rpc_fop_session_create_ops = {
	.fto_fom_init = c2_rpc_fop_session_create_fom_init,
};

struct c2_fop_type_ops c2_rpc_fop_session_destroy_ops = {
	.fto_fom_init = c2_rpc_fop_session_destroy_fom_init,
};

struct c2_fop_type_ops c2_rpc_fop_conn_terminate_ops = {
	.fto_fom_init = c2_rpc_fop_conn_terminate_fom_init,
};

struct c2_fop_type_ops c2_rpc_fop_conn_create_rep_ops = {
	.fto_execute = c2_rpc_fop_conn_create_rep_execute
};

struct c2_fop_type_ops c2_rpc_fop_session_create_rep_ops = {
	.fto_execute = c2_rpc_fop_session_create_rep_execute
};

struct c2_fop_type_ops c2_rpc_fop_session_destroy_rep_ops = {
	.fto_execute = c2_rpc_fop_session_destroy_rep_execute
};

struct c2_fop_type_ops c2_rpc_fop_conn_terminate_rep_ops = {
	.fto_execute = c2_rpc_fop_conn_terminate_rep_execute
};

int c2_rpc_fop_conn_create_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_rpc_fom_conn_create		*fom_cc;
	struct c2_fom				*fom;

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);

	C2_ALLOC_PTR(fom_cc);
	if (fom_cc == NULL)
		return -ENOMEM;

	fop->f_type->ft_fom_type = c2_rpc_fom_conn_create_type;

	fom = &fom_cc->fcc_gen;
	fom->fo_type = &c2_rpc_fom_conn_create_type;
	fom->fo_ops = &c2_rpc_fom_conn_create_ops;

	fom_cc->fcc_fop = fop;
	fom_cc->fcc_fop_rep = c2_fop_alloc(&c2_rpc_fop_conn_create_rep_fopt,
						NULL);
	if (fom_cc->fcc_fop_rep == NULL) {
		c2_free(fom_cc);
		return -ENOMEM;
	}

	*m = fom;
	return 0;
}

int c2_rpc_fop_session_create_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_rpc_fom_session_create	*fom_sc;
	struct c2_fom				*fom;

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);

	C2_ALLOC_PTR(fom_sc);
	if (fom_sc == NULL)
		return -ENOMEM;

	fop->f_type->ft_fom_type = c2_rpc_fom_session_create_type;
	fom = &fom_sc->fsc_gen;
	fom->fo_type = &c2_rpc_fom_session_create_type;
	fom->fo_ops = &c2_rpc_fom_session_create_ops;

	fom_sc->fsc_fop = fop;
	fom_sc->fsc_fop_rep = c2_fop_alloc(&c2_rpc_fop_session_create_rep_fopt,
						NULL);
	if (fom_sc->fsc_fop_rep == NULL) {
		c2_free(fom_sc);
		return -ENOMEM;
	}

	*m = fom;
	return 0;
}

int c2_rpc_fop_session_destroy_fom_init(struct c2_fop *fop,
					struct c2_fom **m)
{
	struct c2_rpc_fom_session_destroy	*fom_sd;
	struct c2_fom				*fom;

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);

	C2_ALLOC_PTR(fom_sd);
	if (fom_sd == NULL)
		return -ENOMEM;

	fop->f_type->ft_fom_type = c2_rpc_fom_session_destroy_type;
	fom = &fom_sd->fsd_gen;
	fom->fo_type = &c2_rpc_fom_session_destroy_type;
	fom->fo_ops = &c2_rpc_fom_session_destroy_ops;

	fom_sd->fsd_fop = fop;
	fom_sd->fsd_fop_rep = c2_fop_alloc(
				&c2_rpc_fop_session_destroy_rep_fopt, NULL);
	if (fom_sd->fsd_fop_rep == NULL) {
		c2_free(fom_sd);
		return -ENOMEM;
	}

	*m = fom;
	return 0;
}

int c2_rpc_fop_conn_terminate_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_rpc_fom_conn_terminate	*fom_ct;
	struct c2_fom				*fom;

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

int c2_rpc_fop_conn_create_rep_execute(struct c2_fop		*fop,
				       struct c2_fop_ctx	*ctx)
{
	c2_rpc_conn_create_reply_received(fop);
	return 0;
}

int c2_rpc_fop_session_create_rep_execute(struct c2_fop		*fop,
					  struct c2_fop_ctx	*ctx)
{
	c2_rpc_session_create_reply_received(fop);
	return 0;
}

int c2_rpc_fop_session_destroy_rep_execute(struct c2_fop	*fop,
					   struct c2_fop_ctx	*ctx)
{
	c2_rpc_session_terminate_reply_received(fop);
	return 0;
}

int c2_rpc_fop_conn_terminate_rep_execute(struct c2_fop		*fop,
					  struct c2_fop_ctx	*ctx)
{
	c2_rpc_conn_terminate_reply_received(fop);
	return 0;
}

/*
 *  REQUEST fops
 */

C2_FOP_TYPE_DECLARE(c2_rpc_fop_conn_create, "rpc_conn_create",
			C2_RPC_FOP_CONN_CREATE_OPCODE,
			&c2_rpc_fop_conn_create_ops);

C2_FOP_TYPE_DECLARE(c2_rpc_fop_conn_terminate, "rpc_conn_terminate",
			C2_RPC_FOP_CONN_TERMINATE_OPCODE,
			&c2_rpc_fop_conn_terminate_ops);

C2_FOP_TYPE_DECLARE(c2_rpc_fop_session_create, "rpc_session_create",
			C2_RPC_FOP_SESSION_CREATE_OPCODE,
			&c2_rpc_fop_session_create_ops);

C2_FOP_TYPE_DECLARE(c2_rpc_fop_session_destroy, "rpc_session_destroy",
			C2_RPC_FOP_SESSION_DESTROY_OPCODE,
			&c2_rpc_fop_session_destroy_ops);

/*
 *  REPLY fops
 */

C2_FOP_TYPE_DECLARE(c2_rpc_fop_conn_create_rep, "rpc_conn_create_reply",
			C2_RPC_FOP_CONN_CREATE_REP_OPCODE,
			&c2_rpc_fop_conn_create_rep_ops);

C2_FOP_TYPE_DECLARE(c2_rpc_fop_conn_terminate_rep, "rpc_conn_terminate_reply",
			C2_RPC_FOP_CONN_TERMINATE_REP_OPCODE,
			&c2_rpc_fop_conn_terminate_rep_ops);

C2_FOP_TYPE_DECLARE(c2_rpc_fop_session_create_rep, "rpc_session_create_reply",
			C2_RPC_FOP_SESSION_CREATE_REP_OPCODE,
			&c2_rpc_fop_session_create_rep_ops);

C2_FOP_TYPE_DECLARE(c2_rpc_fop_session_destroy_rep, "rpc_session_destroy_reply",
			C2_RPC_FOP_SESSION_DESTROY_REP_OPCODE,
			&c2_rpc_fop_session_destroy_rep_ops);

static struct c2_fop_type *fops[] = {
        &c2_rpc_fop_conn_create_fopt,
        &c2_rpc_fop_conn_terminate_fopt,
        &c2_rpc_fop_session_create_fopt,
        &c2_rpc_fop_session_destroy_fopt,
        &c2_rpc_fop_conn_create_rep_fopt,
        &c2_rpc_fop_conn_terminate_rep_fopt,
        &c2_rpc_fop_session_create_rep_fopt,
        &c2_rpc_fop_session_destroy_rep_fopt,
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

