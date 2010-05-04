/* -*- C -*- */
#include <errno.h>

#include "lib/mem.h"
#include "rpc/rpc_svc.h"

/** rpc handlers */
bool c2_session_create_svc(struct session_create_arg *in,
			  struct session_create_ret *out)
{
	struct server *srv;
	struct srv_session *s_sess;

	srv = c2_server_find_by_id(in->sca_server);
	if(!srv) {
		out->error = -ENOSRC;
		return true;
	}

	rc = c2_srv_session_init(src, in, &s_sess);
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

bool c2_session_compound_svc(const struct c2_compound_arg *in,
			    struct c2_compound_reply *out)
{
	struct c2_rpc_server *srv;
	struct c2_srv_session *s_sess;

	srv = c2_server_find_by_id(in->ca_node);
	if(!srv) {
		out->sda_errno = -ENOSRC;
		return true;
	}

	s_sess = c2_srv_session_by_id(in->sa_session);
	if (!s_sess) {
		out->sda_errno = -ENOSRC;
		goto end;
	}

	c2_srv_session_release(s_sess);
end:
	c2_ref_put(s_sess->srvs_ref);
	return true;

}

