/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Alexey Lyashkov <Alexey_Lyashkov@xyratex.com>
 * Original creation date: 04/09/2010
 */

#include <string.h>

#include <lib/list.h>
#include <lib/rwlock.h>
#include <lib/memory.h>

#include <lib/refs.h>

#include <net/net.h>
#include <rpc/rpclib.h>
#include <rpc/rpc_ops.h>

bool c2_session_is_same(const struct c2_session_id *s1, const struct c2_session_id *s2)
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

	srv = container_of(ref, struct c2_rpc_server, rs_ref);

	c2_free(srv);
}

struct c2_rpc_server *c2_rpc_server_create(const struct c2_service_id *srv_id)
{
	struct c2_rpc_server *srv;

	C2_ALLOC_PTR(srv);
	if (!srv)
		return NULL;

	c2_list_link_init(&srv->rs_link);
	c2_ref_init(&srv->rs_ref, 1, c2_rpc_server_free);

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
		c2_list_del(&srv->rs_link);
		need_put = true;
	}
	c2_rwlock_write_unlock(&servers_list_lock);

	if (need_put)
		c2_ref_put(&srv->rs_ref);
}


struct c2_rpc_server *c2_rpc_server_find(const struct c2_service_id *srv_id)
{
	struct c2_rpc_server *srv = NULL;
	bool found = false;

	c2_rwlock_read_lock(&servers_list_lock);
	c2_list_for_each_entry(&servers_list, srv,
			       struct c2_rpc_server, rs_link) {
		if (c2_services_are_same(&srv->rs_id, srv_id)) {
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
static struct c2_list clients_list;

static void rpc_client_free(struct c2_ref *ref)
{
	struct c2_rpc_client *cli;

	cli = container_of(ref, struct c2_rpc_client, rc_ref);

	c2_rwlock_fini(&cli->rc_sessions_lock);
	c2_list_fini(&cli->rc_sessions);
	c2_net_conn_release(cli->rc_netlink);

	c2_free(cli);
}

struct c2_rpc_client *c2_rpc_client_create(const struct c2_service_id *id)
{
	struct c2_rpc_client *cli;

	C2_ALLOC_PTR(cli);
	if(cli == NULL)
		return NULL;

	cli->rc_id = *id;
	c2_ref_init(&cli->rc_ref, 1, rpc_client_free);
	c2_rwlock_init(&cli->rc_sessions_lock);
	c2_list_init(&cli->rc_sessions);

	cli->rc_netlink = c2_net_conn_find(id);
	if (cli->rc_netlink == NULL){
		c2_ref_put(&cli->rc_ref);
		return NULL;
	}

	c2_rwlock_write_lock(&clients_list_lock);
	c2_list_add(&clients_list, &cli->rc_link);
	c2_rwlock_write_unlock(&clients_list_lock);

	return cli;
}

void c2_rpc_client_unlink(struct c2_rpc_client *cli)
{
	bool need_put = false;

	c2_rwlock_write_lock(&clients_list_lock);
	if (c2_list_link_is_in(&cli->rc_link)) {
		c2_list_del(&cli->rc_link);
		need_put = true;
	}
	c2_rwlock_write_unlock(&clients_list_lock);

	if (need_put)
		c2_ref_put(&cli->rc_ref);
}

struct c2_rpc_client *c2_rpc_client_find(const struct c2_service_id *id)
{
	struct c2_rpc_client *cli = NULL;
	bool found = false;

	c2_rwlock_read_lock(&clients_list_lock);
	c2_list_for_each_entry(&clients_list, cli,
			       struct c2_rpc_client, rc_link) {
		if (c2_services_are_same(&cli->rc_id, id)) {
			c2_ref_get(&cli->rc_ref);
			found = true;
			break;
		}
	}
	c2_rwlock_read_unlock(&clients_list_lock);

	return found ? cli : NULL;
}

int c2_rpclib_init()
{
	c2_list_init(&servers_list);
	c2_rwlock_init(&servers_list_lock);

	c2_list_init(&clients_list);
	c2_rwlock_init(&clients_list_lock);

	/** XXX */
//	c2_session_register_ops(rpc_ops);
	return 0;
}

void c2_rpclib_fini()
{
	c2_list_fini(&servers_list);
	c2_rwlock_fini(&servers_list_lock);

	c2_list_fini(&clients_list);
	c2_rwlock_fini(&clients_list_lock);
}
