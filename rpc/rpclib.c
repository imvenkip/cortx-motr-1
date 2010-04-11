/* -*- C -*- */
#include "lib/c2list.h"
#include "lib/cc.h"
#include "lib/memory.h"
#include "lib/refs.h"
#include "rpc/rpc_common.h"

bool session_is_same(struct session_id *s1, struct session_id *s2)
{
	return memcmp(s1, s2, sizeof *s1) == 0;
}

/**
 rpc server group
 */
struct c2_rwlock servers_list_lock;
static struct c2_list servers_list;

static void rpc_server_free(struct c2_ref *ref)
{
	struct rpc_server *srv;

	srv = container_of(ref, struct rpc_server, rs_ref);
	c2_free(srv);
}

struct rpc_server *rpc_server_create(const struct client_id *srv_id)
{
	struct rpc_server *srv;

	srv = c2_alloc(sizeof *srv);
	if (!srv)
		return NULL;

	c2_list_link_init(&srv->rs_link);
	c2_ref_init(&srv->rs_ref, 1, rpc_server_free);
	c2_rwlock_init(&srv->rs_session_lock);
	c2_list_init(&srv->rs_sessions);

	srv->rs_id = *srv_id;

	return srv;
}

void rpc_server_register(struct rpc_server *srv)
{
	c2_ref_get(&srv->rs_ref);

	c2_rwlock_write_lock(&servers_list_lock);
	c2_list_add(&servers_list, &srv->rs_link);
	c2_rwlock_write_unlock(&servers_list_lock);
}

void rpc_server_unregister(struct rpc_server *srv)
{
	bool need_put = FALSE;

	c2_rwlock_write_lock(&servers_list_lock);
	if (c2_list_link_is_in(&srv->rs_link)) {
		c2_list_del(&srv->rs_link);
		need_put = TRUE;
	}
	c2_rwlock_write_unlock(&servers_list_lock);

	if (need_put)
		c2_ref_put(&srv->rs_ref);
}


struct rpc_server *rpc_server_find(const struct client_id *srv_id)
{
	struct c2_list_link *pos;
	struct rpc_server *srv = NULL;
	bool found = FALSE;

	c2_rwlock_read_lock(&servers_list_lock);
	c2_list_for_each(&servers_list, pos) {
		srv = c2_list_entry(pos, struct rpc_server, rs_link);
		if (clients_is_same(&srv->rs_id, srv_id) {
			c2_ref_get(&srv->rs_ref);
			found = TRUE;
			break;
		}
	}
	c2_rwlock_read_unlock(&servers_list_lock);

	return found ? srv : NULL;
}

/**
 rpc client group
 */
struct c2_rwlock clients_list_lock;
static struct c2_list servers_list;

static void rpc_client_free(struct c2_ref *ref)
{
	struct rpc_client *cli;

	cli = container_of(ref, struct rpc_client, rc_ref);
	c2_free(cli);
}

struct rpc_client *rpc_client_create(const struct client_id *id)
{
	struct rpc_client *cli;

	cli = c2_alloc(sizeof *cli);
	if(!cli)
		return NULL;

	c2_list_link_init(&cli->rc_link);
	cli->rc_id = *id;
	c2_ref_init(&cli->cl_ref, 1, rpc_client_free);
	c2_rwlock_init(&cli->rc_sessions_list_lock);
	c2_list_init(&cli->rc_sessions_list);

	c2_rwlock_write_lock(&client_list_lock);
	c2_list_add(&clients_list, &cli->rc_link);
	c2_rwlock_write_unlock(&clients_list_lock);

	return cli;
}

void rpc_client_destroy(struct rpc_client *cli)
{
	bool need_put = FALSE;

	c2_rwlock_write_lock(&clients_list_lock);
	if (c2_list_link_is_in(&cli->rc_link)) {
		c2_list_del(&cli->rc_link);
		need_put = TRUE;
	}
	c2_rwlock_write_unlock(&clients_list_lock);

	if (need_put)
		c2_ref_put(&cli->rc_ref);
}

struct rpc_client *rpc_client_find(const struct client_id *id)
{
	struct c2_list_link *pos;
	struct rpc_client *cli = NULL;
	bool found = FALSE;

	c2_rwlock_read_lock(&client_list_lock);
	c2_list_for_each(&clients_list, pos) {
		cli = c2_list_entry(pos, struct rpc_client, rs_link);
		if (clients_is_same(&cli->rc_id, id) {
			c2_ref_get(&cli->rc_ref);
			found = TRUE;
			break;
		}
	}
	c2_rwlock_read_unlock(&client_list_lock);

	return found ? cli : NULL;

}

void rpclib_init()
{
	c2_list_init(&servers_list);
	с2_rwlock_init(&servers_list_lock);

	c2_list_init(&clients_list);
	c2_rwlock_init(&clients_list_lock);

}
