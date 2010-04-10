/* -*- C -*- */
#include "lib/c2list.h"

#include "rpc_common.h"
#include "session.h"
#include "session_proto.h"

static void session_dtor(struct c2_ref *ref)
{
	struct c2_cli_session *sess;

	sess = container_of(ref, struct c2_cli_session, sess_ref);

	c2_free(sess);
}

static void session_create_cb(struct session_create_arg *arg,
			      struct session_create_ret *ret,
			      const struct rpc_client cli,
			      CLIENT *net)
{
	struct c2_cli_session *cli_s;

	if (ret->errno)
		return;

	cli_s = c2_alloc(sizeof *cli_s);
	if (!cli_s)
		return;

	c2_list_link_init(&cli_s->sess_link);
	cli_s->cli = net;
	cli_s->sess_srv = arg->ssa_server;
	cli_s->sess_id = ret->session_create_ret_u.sco_session_id;

	session_slot_table_adjust(&cli_s->sess_slots,
				  ret->session_create_ret_u.sco_high_slot_id);
	c2_ref_init(&cli_s->sess_ref, 1, session_dtor);

	c2_rwlock_read_lock(&cli->rc_sessions_lock);
	c2_list_add(&cli->rc_sessions, &cli_s->sess_link);
	c2_rwlock_write_lock(&cli->rc_sessions_lock);
}

int c2_session_cli_create(const struct rpc_client *cli, const client_id * srv_uuid,
			  CLIENT * net);
{
	struct session_create_arg req;
	struct session_create_ret ret;

	req.sca_client = cli->cl_id;
	req.sca_server = srv_uuid;
	req.sca_high_slot_id = C2_MAX_SLOTS;
	/* XXX need change later */
	req.cca_max_rpc_size = 0;

	/* call rpc */
	session_create_cb(&arg, &ret, cli, net);
}

void session_destroy_cb(struct session_destroy_ret *ret,
			const struct c2_cli_session *sess)
{
	session_slot_table_adjust(&sess->sess_slots, 0);
	/* XXX wait until all rpc's finished */

	c2_rwlock_write_lock(&cli->rc_sessions_lock);
	c2_list_del(&sess->sess_link);
	c2_rwlock_write_unlock(&cli->rc_sessions_lock);

	c2_ref_put(&sess->sess_ref);
}

int c2_session_cli_destroy(const struct c2_cli_session *sess)
{
	struct session_destroy_arg arg;

	arg.da_session_id = sess.sess_id;

	/* call rpc */
	session_destroy_cb(&ret, sess);
}

struct c2_cli_session *c2_session_cli_find(const struct rpc_client *cli,
					   const struct client_id * srv_uuid)
{
	struct c2_list_link *pos;
	struct c2_cli_session *sess = NULL;
	bool found = FALSE;

	c2_rwlock_read_lock(&cli->cli_sessions_lock);
	c2_list_for_each(&cli->sessions, pos) {
		sess = c2_list_entry(pos, struct c2_cli_session, sess_link);
		if (client_is_same(&sess->sess_srv, srv_uuid)) {
			c2_ref_get(&sess->sess_ref);
			found = TRUE;
			break;
		}
	}
	c2_rwlock_read_unlock(&cli->cli_sessions_lock);
	
	return found ? sess : NULL;
}

int c2_session_check(struct rpc_client *cli, const struct c2_cli_session *sess)
{
	struct c2_cli_slot *slot;

	slot = find_empty_slot(sess);
	/* need handle delayed queue */
	if (!slot)
		return -ENOSPC;

	rc = compound_send_seq(slot, 0, NULL);
	c2_ref_put(&sess->sess_ref);

	return rc;
}
