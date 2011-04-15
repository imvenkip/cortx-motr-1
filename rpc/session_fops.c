#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "session_fops.h"
#include "lib/errno.h"
#include "fop/fom.h"
#include "fop/fop.h"
#include "session_u.h"
#include "fop/fop_iterator.h"

extern struct c2_fop_type_format c2_rpc_conn_create_tfmt;
extern struct c2_fop_type_format c2_rpc_conn_terminate_tfmt;
extern struct c2_fop_type_format c2_rpc_session_create_tfmt;
extern struct c2_fop_type_format c2_rpc_session_destroy_tfmt;

extern struct c2_fop_type_format c2_rpc_conn_create_rep_tfmt;
extern struct c2_fop_type_format c2_rpc_conn_terminate_rep_tfmt;
extern struct c2_fop_type_format c2_rpc_session_create_rep_tfmt;
extern struct c2_fop_type_format c2_rpc_session_destroy_rep_tfmt;


int c2_rpc_fom_init(struct c2_fop *fop, struct c2_fom **m);
/*
int c2_rpc_conn_terminate_fom_init(struct c2_fop *fop, struct c2_fom **m);

int c2_rpc_session_create_fom_init(struct c2_fop *fop, struct c2_fom **m);

int c2_rpc_session_destroy_fom_init(struct c2_fop *fop, struct c2_fom **m);
*/

struct c2_fop_type_ops c2_rpc_conn_create_ops = {
	.fto_fom_init = c2_rpc_fom_init,
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

void rpc_fop_fini(void)
{
        c2_fop_object_fini();
        c2_fop_type_fini_nr(fops, ARRAY_SIZE(fops));
}

int rpc_fop_init(void)
{
	int result;

	result = c2_fop_type_build_nr(fops, ARRAY_SIZE(fops));
	if (result != 0)
		rpc_fop_fini();
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

