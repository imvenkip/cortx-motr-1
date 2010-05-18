/* -*- C -*- */
#include <errno.h>

#include "lib/mem.h"
#include "rpc/rpc_ops.h"
#include "rpc/session_types.h"
#include "rpc/session_svc.h"

static struct c2_rpc_op  create_session = {
	.ro_op = C2_SESSION_CREATE,
	.ro_arg_size = sizeof(struct c2_session_create_arg),
	.ro_xdr_arg = (c2_xdrproc_t)c2_xdr_session_create_arg,
	.ro_result_size = sizeof(struct c2_session_create_ret),
	.ro_xdr_result = (c2_xdrproc_t)c2_xdr_session_create_ret,
	.ro_handler = C2_RPC_SRV_PROC(c2_session_create_svc)
};

static struct c2_rpc_op  destroy_session = {
	.ro_op = C2_SESSION_DESTROY,
	.ro_arg_size = sizeof(struct c2_session_destroy_arg),
	.ro_xdr_arg = (c2_xdrproc_t)c2_xdr_session_destroy_arg,
	.ro_result_size = sizeof(struct c2_session_destroy_ret),
	.ro_xdr_result = (c2_xdrproc_t)c2_xdr_session_destroy_ret,
	.ro_handler = C2_RPC_SRV_PROC(c2_session_destroy_svc)
};



int c2_session_register_ops(struct c2_rpc_op_table *ops)
{
	rc = c2_rpc_op_register(ops, &create_sessions);
	rc = c2_rpc_op_register(ops, &destroy_sessions);
}


/** rpc handlers */
bool c2_session_create_svc(struct session_create_arg *in,
			  struct session_create_ret *out)
{
	struct c2_rpc_server *srv;
	struct c2_srv_session *s_sess;

	srv = c2_server_find_by_id(in->sca_server);
	if(!srv) {
		out->error = -ENOSRC;
		return true;
	}

	rc = c2_server_session_create(srv, &s_sess);
	if (rc < 0) {
		out->error = rc;
		return true;
	}

	/* create reply */
	out->errno = 0;
	out->session_create_ret_u.reply.sco_session_id = s_sess->srvs_id;
	out->session_create_ret_u.reply.sco_high_slot_id =
		s_sess->srvs_slots.srvs_high_slot_id;
	c2_srv_session_release(s_sess);
out:
	c2_ref_put(&srv->srv_ref);
	return true;
}

bool c2_session_destroy_svc(const struct session_destroy_arg *in,
			   struct session_destroy_out *out)
{
	struct c2_rpc_server *srv;
	struct c2_srv_session *s_sess;

	srv = c2_server_find_by_id(in->da_node);
	if(!srv) {
		out->sda_errno = -ENOSRC;
		return true;
	}

	s_sess = c2_srv_session_by_id(in->da_session);
	if (!s_sess) {
		out->sda_errno = -ENOSRC;
		goto end;
	}

	c2_srv_session_unlink(s_sess);
	c2_srv_session_release(s_sess);
end:
	c2_ref_put(s_sess->srvs_ref);
	return true;
}

