/* -*- C -*- */
#include <errno.h>

#include "lib/atomic.h"
#include "lib/memory.h"

#include "net/net_types.h"

#include "rpc/session_srv.h"

int c2_server_session_init(struct c2_rpc_server *srv)
{
	int rc;

	rc = c2_cache_init(&srv->rs_sessions);
	if (rc < 0)
		goto out;

	rc = c2_rpc_op_register(&srv->rs_ops, c2_sesson_create_op);
	if (rc < 0)
		goto out;

	rc = c2_rpc_op_register(&srv->rs_ops, c2_sesson_destroy_op);
out:
	return rc;
}

void c2_server_session_fini(struct c2_rpc_server *srv)
{
	c2_cache_fini(&srv->rs_session);
}



static void server_session_new_id(struct c2_rpc_server *srv)
{

}

/*** ***/
static void c2_srv_session_free(struct c2_ref *ref)
{
	struct c2_cli_session *sess;

	sess = container_of(ref, struct c2_srv_session, sess_ref);

	C2_FREE_PTR(sess);
}

int c2_srv_session_init(struct c2_rpc_server *srv, uint32_t highslot,
			struct c2_service_id *cli_id;
			struct c2_srv_session **sess)
{
	struct c2_srv_session *s_sess;

	C2_ALLOC_PTR(s_sess);
	if (!s_sess) {
		return -ENOMEM;
	}

	c2_list_link_init(&s_sess->srvs_link);
	/* 2 = add + create */
	c2_ref_init(&s_sess->srvs_ref, 2, c2_srv_session_free);
	srv_session_new_id(srv, &s_sess->srvs_id);
	s_sess->srvs_cli = *cli_id;
	/* init slot table */
	s_sess->srvs_slots.srvst_high_slot_id = high_slot_id;

	c2_rwlock_write_lock(&srv->srv_session_lock);
	c2_list_add(&srv->srv_session, &s_sess->srvs_link);
	c2_rwlock_write_unlock(&srv->srv_session_lock);

	*sess = s_sess;
	return 0;
}

void c2_srv_session_unlink(struct c2_rpc_server *srv, struct c2_srv_session *sess)
{
	bool need_put = false;

	c2_rwlock_write_lock(&srv->srv_session_lock);
	if (c2_list_link_is_in(&ses->srvs_link)) {
		c2_list_del_init(&sess_srvs_link);
		need_put = true;
	}
	c2_rwlock_write_unlock(&srv->srv_session_lock);

	if (need_put)
		c2_ref_put(&sess->srvs_ref);
}

struct c2_srv_session *c2_srv_session_find_by_id(struct c2_rpc_server *srv,
						 const c2_session_id *ss_id)
{
	struct c2_srv_session *sess;
	bool found;

	c2_rwlock_read_lock(&srv->srv_session_lock);
	c2_list_foreach_entry(&srv->srv_session, sess,
			      struct c2_srv_session, srvs_link) {
		if (c2_session_is_same(&sess->srvs_id, ss_id)) {
			c2_ref_get(&sess->srvs_ref);
			break;
		}
	}
	c2_rwlock_read_unlock(&srv->srv_session_lock);

	return found ? sess : NULL;
}

void c2_srv_session_release(struct c2_srv_session *sess)
{
	c2_ref_put(&sess->srvs_ref);
}
