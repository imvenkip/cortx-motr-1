/* -*- C -*- */
#include <errno.h>

#include <lib/memory.h>
#include <net/net.h>

#include <rpc/rpc_ops.h>

#include <rpc/session_types.h>
#include <rpc/session_svc.h>
#include <rpc/xdr/session.h>

static struct c2_rpc_op  create_session = {
	.ro_op = C2_SESSION_CREATE,
	.ro_arg_size = sizeof(struct c2_session_create_arg),
	.ro_xdr_arg = (c2_xdrproc_t)c2_xdr_session_create_arg,
	.ro_result_size = sizeof(struct c2_session_create_ret),
	.ro_xdr_result = (c2_xdrproc_t)c2_xdr_session_create_ret,
	.ro_handler = c2_session_create_svc
};

static struct c2_rpc_op  destroy_session = {
	.ro_op = C2_SESSION_DESTROY,
	.ro_arg_size = sizeof(struct c2_session_destroy_arg),
	.ro_xdr_arg = (c2_xdrproc_t)c2_xdr_session_destroy_arg,
	.ro_result_size = sizeof(struct c2_session_destroy_ret),
	.ro_xdr_result = (c2_xdrproc_t)c2_xdr_session_destroy_ret,
	.ro_handler = c2_session_destroy_svc
};


int c2_session_register_ops(struct c2_rpc_op_table *ops)
{
	int rc;

	rc = c2_rpc_op_register(ops, &create_session);
	if (rc < 0)
		return rc;

	rc = c2_rpc_op_register(ops, &destroy_session);

	return rc;
}


/** rpc handlers */
bool c2_session_create_svc(const struct c2_rpc_op *op, void *in, void **out)
{
	return true;
}

bool c2_session_destroy_svc(const struct c2_rpc_op *op, void *in, void **out)
{
	return true;
}

