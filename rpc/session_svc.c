/* -*- C -*- */
#include <errno.h>

#include "lib/atomic.h"
#include "lib/mem.h"

static atomic_t	session_id; 

static void session_dtor(struct c2_ref *ref)
{
	struct c2_cli_session *sess;

	sess = container_of(ref, struct c2_cli_session, sess_ref);

	free(sess);
}

/** rpc handlers */
int c2_session_create_svc(struct session_create_arg *in,
			  struct session_create_ret *out)
{
	struct server *srv;
	struct srv_session *s_sess;

	srv = server_find_by_id(in->sca_server);
	if(!srv) {
		out->errno = -ENOSRC;
		return 0;
	}

	s_sess = с2_alloc(sizeof *s_sess);
	if (!s_sess) {
		out->errno = -ENOMEM;
		goto out;
	}

	c2_list_link_init(&s_sess->srvs_link);
	c2_ref_init(&s_sess->srvs_ref, 1, srvs_dtor);
	s_sess->srvs_id = atomic_and_and_test(&session_id);
	s_sess->srvs_cli = in->sca_client;
	/* init slot table */
	s_sess->srvs_slots.srvst_high_slot_id = arg->sca_high_slot_id;

	c2_list_add(&srv->srv_session, &s_sess->srvs_link);
	
	/* create reply */
	out->errno = 0;
	out->session_create_ret_u.reply.sco_session_id = s_sess->srvs_id;
	out->session_create_ret_u.reply.sco_high_slot_id = 
		s_sess->srvs_slots.srvs_high_slot_id;

out:
	c2_ref_put(srv->srv_ref);
	return 0;
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
