/* -*- C -*- */
#include "lib/c2list.h"
#include "lib/cc.h"
#include "lib/memory.h"
#include "lib/refs.h"
#include "rpc/rpc_common.h"

bool c2_session_is_same(struct session_id const *s1, struct session_id const *s2)
{
	return memcmp(s1, s2, sizeof *s1) == 0;
}

/**
 rpc server group
 */
struct c2_rwlock servers_list_lock;
static struct c2_list servers_list;

static void c2_rpc_server_free(struct c2_ref *ref)
{
	struct c2_rpc_server *srv;

	srv = container_of(ref, struct rpc_server, rs_ref);
	C2_FREE_PTR(srv);
}

struct c2_rpc_server *c2_rpc_server_init(const struct client_id *srv_id)
{
	struct rpc_server *srv;

	C2_ALLOC_PTR(srv);
	if (!srv)
		return NULL;

	c2_list_link_init(&srv->rs_link);
	c2_ref_init(&srv->rs_ref, 1, c2_rpc_server_free);
	c2_rwlock_init(&srv->rs_session_lock);
	c2_list_init(&srv->rs_sessions);

	srv->rs_id = *srv_id;

	return srv;
}

void c2_rpc_server_register(struct c2_rpc_server *srv)
{
	c2_ref_get(&srv->rs_ref);

	c2_rwlock_write_lock(&servers_list_lock);
	c2_list_add(&servers_list, &srv->rs_link);
	c2_rwlock_write_unlock(&servers_list_lock);
}

void c2_rpc_server_unregister(struct c2_rpc_server *srv)
{
	bool need_put = false;

	c2_rwlock_write_lock(&servers_list_lock);
	if (c2_list_link_is_in(&srv->rs_link)) {
		c2_list_del_init(&srv->rs_link);
		need_put = true;
	}
	c2_rwlock_write_unlock(&servers_list_lock);

	if (need_put)
		c2_ref_put(&srv->rs_ref);
}


struct rpc_server *c2_rpc_server_find(struct client_id const *srv_id)
{
	struct c2_list_link *pos;
	struct c2_rpc_server *srv = NULL;
	bool found = false;

	c2_rwlock_read_lock(&servers_list_lock);
	c2_list_for_each_entry(&servers_list, srv,
			       struct c2_rpc_server, rs_link) {
		if (c2_node_is_same(&srv->rs_id, srv_id)) {
			c2_ref_get(&srv->rs_ref);
			found = true;
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

static void c2_rpc_client_free(struct c2_ref *ref)
{
	struct c2_rpc_client *cli;

	cli = container_of(ref, struct rpc_client, rc_ref);
	C2_FREE_PTR(cli);
}

struct c2_rpc_client *c2_rpc_client_create(const struct c2_service_id *id)
{
	struct c2_rpc_client *cli;

	C2_ALLOC_PTR(cli);
	if(!cli)
		return NULL;

	c2_list_link_init(&cli->rc_link);
	cli->rc_id = *id;
	c2_ref_init(&cli->cl_ref, 1, rpc_client_free);
	c2_rwlock_init(&cli->rc_sessions_list_lock);
	c2_list_init(&cli->rc_sessions_list);

	c2_rwlock_write_lock(&client_list_lock);
	/*XXX */
	c2_list_add(&clients_list, &cli->rc_link);
	c2_rwlock_write_unlock(&clients_list_lock);

	return cli;
}

void c2_rpc_client_destroy(struct c2_rpc_client *cli)
{
	bool need_put = false;

	c2_rwlock_write_lock(&clients_list_lock);
	if (c2_list_link_is_in(&cli->rc_link)) {
		c2_list_del_init(&cli->rc_link);
		need_put = true;
	}
	c2_rwlock_write_unlock(&clients_list_lock);

	if (need_put)
		c2_ref_put(&cli->rc_ref);
}

struct c2_rpc_client *c2_rpc_client_find(struct node_id const *id)
{
	struct c2_rpc_client *cli = NULL;
	bool found = false;

	c2_rwlock_read_lock(&client_list_lock);
	c2_list_for_each_entry(&clients_list, cli,
			       struct rpc_client, rs_link) {
		cli = c2_list_entry(pos, struct rpc_client, rs_link);
		if (c2_node_is_same(&cli->rc_id, id)) {
			c2_ref_get(&cli->rc_ref);
			found = true;
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

void rpclib_fini()
{
	c2_list_fini(&servers_list);
	с2_rwlock_fini(&servers_list_lock);

	c2_list_fini(&clients_list);
	c2_rwlock_fini(&clients_list_lock);
}
