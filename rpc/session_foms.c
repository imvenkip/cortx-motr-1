#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fop/fop.h"
#include "session_foms.h"
#include "session_fops.h"
#include "stob/stob.h"
#include "lib/errno.h"
#include "net/net.h"
#include "lib/memory.h"
#ifdef __KERNEL__
#include "session_k.h"
#else
#include "session_u.h"
#endif

#include "fop/fop_format_def.h"
#include "session.ff"

#ifndef __KERNEL__

static struct c2_fom_ops c2_rpc_fom_conn_create_ops = {
	.fo_fini	= NULL,
	.fo_state	= c2_rpc_fom_state,
};

static struct c2_fom_ops c2_rpc_fom_conn_terminate_ops = {
	.fo_fini	= NULL,
	.fo_state	= c2_rpc_fom_state,
};

static struct c2_fom_ops c2_rpc_fom_session_create_ops = {
	.fo_fini	= NULL,
	.fo_state	= c2_rpc_fom_state,
};

static struct c2_fom_ops c2_rpc_fom_session_destroy_ops = {
	.fo_fini	= NULL,
	.fo_state	= c2_rpc_fom_state,
};

/*
static struct c2_fom_ops c2_rpc_fom_rep_ops = {
	.fo_fini	= NULL,
	.fo_state	= NULL,
};
*/

static const struct c2_fom_type_ops c2_rpc_conn_create_type_ops = {
	.fto_create	= NULL,
};

static const struct c2_fom_type_ops c2_rpc_conn_terminate_type_ops = {
	.fto_create	= NULL,
};

static const struct c2_fom_type_ops c2_rpc_session_create_type_ops = {
	.fto_create	= NULL,
};

static const struct c2_fom_type_ops c2_rpc_session_destroy_type_ops = {
	.fto_create	= NULL,
};

static struct c2_fom_type c2_rpc_fom_conn_create_mopt = {
	.ft_ops		= &c2_rpc_conn_create_type_ops,
};

static struct c2_fom_type c2_rpc_fom_conn_terminate_mopt = {
	.ft_ops		= &c2_rpc_conn_terminate_type_ops,
};

static struct c2_fom_type c2_rpc_fom_session_create_mopt = {
	.ft_ops		= &c2_rpc_session_create_type_ops,
};

static struct c2_fom_type c2_rpc_fom_session_destroy_mopt = {
	.ft_ops		= &c2_rpc_session_destroy_type_ops,
};

/**
   An array
 */

static struct c2_fom_type *c2_rpc_fom_types[] = {
	&c2_rpc_fom_conn_create_mopt,
	&c2_rpc_fom_conn_terminate_mopt,
	&c2_rpc_fom_session_create_mopt,
	&c2_rpc_fom_session_destroy_mopt,
};

struct c2_fom_type *c2_rpc_fom_type_map(c2_fop_type_code_t code)
{
	C2_PRE(IS_IN_ARRAY((code - c2_rpc_conn_create_opcode),
				c2_rpc_fom_types));
	return c2_rpc_fom_types[code - c2_rpc_conn_create_opcode];
}

int c2_rpc_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_fom		*fom;
	struct c2_rpc_fom_ctx	*fom_ctx;
	struct c2_fom_type	*fom_type;	

	C2_PRE(fop != NULL);
	C2_PRE(m != NULL);
	
	fom_ctx = c2_alloc(sizeof(struct c2_rpc_fom_ctx));
	if(fom_ctx == NULL)
		return -ENOMEM;
	fom_type = c2_rpc_fom_type_map(fop->f_type->ft_code);
	C2_ASSERT(fom_type != NULL);
	fop->f_type->ft_fom_type = *fom_type;
	fom = &fom_ctx->rfc_fom_gen;
	fom->fo_type = fom_type;

	if(fop->f_type->ft_code == c2_rpc_conn_create_opcode) {
		fom->fo_ops = &c2_rpc_fom_conn_create_ops;
		fom_ctx->rfc_rep_fop =
			c2_fop_alloc(&c2_rpc_conn_create_rep_fopt, NULL);
	}
	else if(fop->f_type->ft_code == c2_rpc_conn_terminate_opcode) {
		fom->fo_ops = &c2_rpc_fom_conn_terminate_ops;
		fom_ctx->rfc_rep_fop =
			c2_fop_alloc(&c2_rpc_conn_terminate_rep_fopt, NULL);
	}
	else if(fop->f_type->ft_code == c2_rpc_session_create_opcode) {
		fom->fo_ops = &c2_rpc_fom_session_create_ops;
		fom_ctx->rfc_rep_fop =
			c2_fop_alloc(&c2_rpc_session_create_rep_fopt, NULL);
	}
	else if(fop->f_type->ft_code == c2_rpc_session_destroy_opcode) {
		fom->fo_ops = &c2_rpc_fom_session_destroy_ops;
		fom_ctx->rfc_rep_fop =
			c2_fop_alloc(&c2_rpc_session_destroy_rep_fopt, NULL);
	}
	if(fom_ctx->rfc_rep_fop == NULL) {
		c2_free(fom_ctx);
		return -ENOMEM;
	}
	fom_ctx->rfc_fop = fop;
	*m = &fom_ctx->rfc_fom_gen;
	return 0;
}

int c2_rpc_fom_state(struct c2_fom *fom)
{
	struct c2_rpc_fom_ctx 			*fom_ctx;
	struct c2_rpc_conn_create 		*rpc_conn_create_fop;
	struct c2_rpc_conn_terminate 		*rpc_conn_terminate_fop;
	struct c2_rpc_session_create		*rpc_session_create_fop;
	struct c2_rpc_session_destroy		*rpc_session_destroy_fop;
	struct c2_rpc_conn_create_rep		*rpc_conn_create_rep_fop;
	struct c2_rpc_conn_terminate_rep	*rpc_conn_terminate_rep_fop;
	struct c2_rpc_session_create_rep	*rpc_session_create_rep_fop;
	struct c2_rpc_session_destroy_rep	*rpc_session_destroy_rep_fop;


	C2_PRE(fom != NULL);

	fom_ctx = container_of(fom, struct c2_rpc_fom_ctx, rfc_fom_gen);	
	if(fom_ctx->rfc_fop->f_type->ft_code == c2_rpc_conn_create_opcode) {
		rpc_conn_create_fop = c2_fop_data(fom_ctx->rfc_fop);
		rpc_conn_create_rep_fop = c2_fop_data(fom_ctx->rfc_rep_fop);
		rpc_conn_create_rep_fop->rccr_rc = 10;
		rpc_conn_create_rep_fop->rccr_snd_id = 10;
		fom->fo_phase = FOPH_RPC_CONN_CREATE;
	}
	else if(fom_ctx->rfc_fop->f_type->ft_code == c2_rpc_conn_terminate_opcode) {
		rpc_conn_terminate_fop = c2_fop_data(fom_ctx->rfc_fop);
		rpc_conn_terminate_rep_fop = c2_fop_data(fom_ctx->rfc_rep_fop);
		rpc_conn_terminate_rep_fop->ctr_rc = 20;
		fom->fo_phase = FOPH_RPC_CONN_TERMINATE;
	}
	else if(fom_ctx->rfc_fop->f_type->ft_code == c2_rpc_session_create_opcode) {
		rpc_session_create_fop = c2_fop_data(fom_ctx->rfc_fop);
		rpc_session_create_rep_fop = c2_fop_data(fom_ctx->rfc_rep_fop);
		rpc_session_create_rep_fop->rscr_status = 30;
		rpc_session_create_rep_fop->rscr_session_id = 30;
		fom->fo_phase = FOPH_RPC_SESSION_CREATE;
	}
	else if(fom_ctx->rfc_fop->f_type->ft_code == c2_rpc_session_destroy_opcode) {
		rpc_session_destroy_fop = c2_fop_data(fom_ctx->rfc_fop);
		rpc_session_destroy_rep_fop = c2_fop_data(fom_ctx->rfc_rep_fop);
		rpc_session_destroy_rep_fop->rsdr_status = 40;
	}
	
	c2_net_reply_post(fom->fo_fop_ctx->ft_service, fom_ctx->rfc_rep_fop,
				fom->fo_fop_ctx->fc_cookie);
	fom_ctx->rfc_fom_gen.fo_phase = FOPH_DONE;
	c2_rpc_fom_fini(&fom_ctx->rfc_fom_gen);
	return FSO_AGAIN;
}

void c2_rpc_fom_fini(struct c2_fom *fom)
{
	struct c2_rpc_fom_ctx *fom_ctx;
	fom_ctx = container_of(fom, struct c2_rpc_fom_ctx, rfc_fom_gen);
	c2_free(fom_ctx);
}

int c2_rpc_dummy_req_handler(struct c2_service *s, struct c2_fop *fop,
			void *cookie, struct c2_fol *fol,
			struct c2_stob_domain *dom)
{
	struct c2_fop_ctx       ctx;
	int                     result = 0;
	struct c2_fom          *fom = NULL;

	ctx.ft_service = s;
	ctx.fc_cookie  = cookie;
	result = fop->f_type->ft_ops->fto_fom_init(fop, &fom);
	C2_ASSERT(fom != NULL);

	fom->fo_domain = dom;
	fom->fo_fop_ctx = &ctx;
	fom->fo_fol = fol;

	/* 
	 * Start the FOM.
	 */
	return fom->fo_ops->fo_state(fom);
}

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

