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

int c2_rpc_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	printf("rpc_fom_init called\n");
	return 0;
}
/*
int c2_rpc_conn_terminate_fom_init(struct c2_fop *fop, struct c2_fom **m);

int c2_rpc_session_create_fom_init(struct c2_fop *fop, struct c2_fom **m);

int c2_rpc_session_destroy_fom_init(struct c2_fop *fop, struct c2_fom **m);
*/

int c2_rpc_conn_create_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	struct c2_rpc_fom_conn_create		*fom_obj;
	struct c2_fom				*fom;

	C2_PRE(fop != NULL);
	C2_PRE(fom != NULL);

	printf ("conn_create fom init called\n");
	C2_ALLOC_PTR(fom_obj);
	if (fom_obj == NULL)
		return -ENOMEM;

	fop->f_type->ft_fom_type = c2_rpc_fom_conn_create_type;
	fom = &fom_obj->fcc_gen;
	fom->fo_type = &c2_rpc_fom_conn_create_type;
	fom->fo_ops = &c2_rpc_fom_conn_create_ops;

	fom_obj->fcc_fop_rep = c2_fop_alloc(&c2_rpc_conn_create_fopt, NULL);
	if (fom_obj->fcc_fop_rep == NULL) {
		c2_free(fom_obj);
		return -ENOMEM;
	}
	fom_obj->fcc_dom = (struct c2_cob_domain *)0xABCDEF;

	*m = fom;
	printf ("conn_create fom init call finished\n");
	return 0;
}
struct c2_fop_type_ops c2_rpc_conn_create_ops = {
	.fto_fom_init = &c2_rpc_conn_create_fom_init,
};

struct c2_fop_type_ops c2_rpc_conn_terminate_ops = {
	.fto_fom_init = c2_rpc_fom_init,
};

struct c2_fop_type_ops c2_rpc_session_create_ops = {
	.fto_fom_init = c2_rpc_fom_init,
};

struct c2_fop_type_ops c2_rpc_session_destroy_ops = {
	.fto_fom_init = c2_rpc_fom_init,
};

static int c2_rpc_rep_fom_init(struct c2_fop *fop, struct c2_fom **m)
{
	return 0;
}

struct c2_fop_type_ops c2_rpc_rep_ops = {
	.fto_fom_init = c2_rpc_rep_fom_init,
};

/** 
   REQUEST
 */

C2_FOP_TYPE_DECLARE(c2_rpc_conn_create, "RPC CONN CREATE REQUEST",
			c2_rpc_conn_create_opcode, &c2_rpc_conn_create_ops);
C2_FOP_TYPE_DECLARE(c2_rpc_conn_terminate, "RPC CONN TERMINATE REQUEST",
			c2_rpc_conn_terminate_opcode, &c2_rpc_conn_terminate_ops);
C2_FOP_TYPE_DECLARE(c2_rpc_session_create, "RPC SESSION CREATE REQUEST",
			c2_rpc_session_create_opcode, &c2_rpc_session_create_ops);
C2_FOP_TYPE_DECLARE(c2_rpc_session_destroy, "RPC SESSION DESTROY REQUEST",
			c2_rpc_session_destroy_opcode, &c2_rpc_session_destroy_ops);

/**
   REPLY
 */

C2_FOP_TYPE_DECLARE(c2_rpc_conn_create_rep, "RPC CONN CREATE REQUEST",
			c2_rpc_conn_create_rep_opcode, &c2_rpc_rep_ops);
C2_FOP_TYPE_DECLARE(c2_rpc_conn_terminate_rep, "RPC CONN TERMINATE REQUEST",
			c2_rpc_conn_terminate_rep_opcode, &c2_rpc_rep_ops);
C2_FOP_TYPE_DECLARE(c2_rpc_session_create_rep, "RPC SESSION CREATE REQUEST",
			c2_rpc_session_create_rep_opcode, &c2_rpc_rep_ops);
C2_FOP_TYPE_DECLARE(c2_rpc_session_destroy_rep, "RPC SESSION DESTROY REQUEST",
			c2_rpc_session_destroy_rep_opcode, &c2_rpc_rep_ops);

static struct c2_fop_type *fops[] = {
        &c2_rpc_conn_create_fopt,
        &c2_rpc_conn_terminate_fopt,
        &c2_rpc_session_create_fopt,
        &c2_rpc_session_destroy_fopt,
        &c2_rpc_conn_create_rep_fopt,
        &c2_rpc_conn_terminate_rep_fopt,
        &c2_rpc_session_create_rep_fopt,
        &c2_rpc_session_destroy_rep_fopt,
};

void c2_rpc_session_fop_fini(void)
{
	printf("session fop fini called\n");
        c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
}

int c2_rpc_session_fop_init(void)
{
	int result;

	printf("Session fop init called\n");
	result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
	if (result != 0)
		c2_rpc_session_fop_fini();
	return result;
}

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

