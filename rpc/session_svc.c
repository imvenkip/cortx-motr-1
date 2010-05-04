/* -*- C -*- */
#include <errno.h>

#include "lib/atomic.h"
#include "lib/mem.h"

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
	c2_ref_put(&s_ses->srvs_ref);

out:
	c2_ref_put(&srv->srv_ref);
	return true;
}

int c2_session_destroy_svc(struct session_destroy_arg *in,
			   struct session_destroy_out *out)
{
	struct srv_session *s_sess;

	s_sess = find_srv_session_by_uid(in->da_session_id);
	if (!s_sess) {
		return -ENOSRC;
	}

	c2_ref_put(s_sess->srvs_ref);
}

int c2_session_compound_svc(struct session_compound_arg *in,
			    struct session_compound_reply *out)
{
	struct srv_session *s_sess;

	s_sess = find_srv_session_by_id(in->da_session_id);
	if (!s_sess) {
		return -ENOSRC;
	}

	c2_ref_put(s_sess->srvs_ref);
}

int c2_session_adjust_svc(struct c2_session_adjust_in *arg,
			  struct c2_session_adjust_out *out)
{
	struct c2_cli_session *c_sess;

	c_sess = find_cli_session_by_id(arg->da_session_id);
	if (!c_sess) {
		return -ENOSRC;
	}

	c2_ref_put(c_sess->srvs_ref);
}
