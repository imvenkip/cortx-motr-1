#include "lib/cc.h"
#include "lib/c2list.h"
#include "lib/memory.h"

#include "net/net.h"
#include "net/net_internal.h"

#include <errno.h>
#include <rpc/clnt.h>

struct c2_rw_lock conn_list_lock;
struct c2_list   conn_list;

int c2_net_connection_create(struct node_id *nid, long prgid, char *nn)
{
	struct c2_net_conn *conn;
	struct CLIENT *cli;

	conn = c2_alloc(sizeof *conn);
	if (!conn)
		return -ENOMEM;

	cli = clnt_create (nn, prgid, 1, "tcp");
	if (!cli) {
		c2_free(conn);
		return -ENOCONN;
	}

	c2_list_link_init(&conn->nc_link);
	conn->nc_id = nid;
	conn->nc_prgid = prgid;
	c2_ref_init(&conn->nc_refs, 1, c2_net_conn_free_cb);
	conn->nc_cli = cli;

	c2_rwlock_write_lock(&conn_list_lock);
	c2_list_add(&conn_list, &conn->nc_link);
	c2_rwlock_write_unlock(&conn_list_lock);

	return 0;
}

struct c2_net_conn *c2_net_connection_find(struct node_id *nid, long prgid)
{
	struct c2_list_link *pos;
	struct c2_net_conn *conn;
	bool found = FALSE;

	c2_rwlock_read_lock(&conn_list_lock);
	c2_list_for_each(&conn_list, pos) {
		conn = c2_list_entry(pos, struct c2_net_conn, nc_link);

		if (conn->nc_prg_id == prgid &&
		    nodes_is_same(conn->nc_id, nid)) {
			c2_ref_get(&conn->nc_refs);
			found = TRUE;
			break;
		}
	}
	c2_rwlock_read_unlock(&conn_list_lock);

	return found ? conn : NULL;
}

void c2_net_conn_release(struct c2_net_conn *conn)
{
	c2_ref_put(&conn->nc_refs);
}

int c2_net_conn_destroy(struct c2_net_conn *conn)
{
	bool need_put = FALSE;

	c2_rwlock_write_lock(&conn_list_lock);
	if (c2_link_is_in(&conn->nc_link)) {
		c2_list_del(&conn->nc_link);
		need_put = TRUE;
	}
	c2_rwlock_write_lock(&conn_list_lock);

	if (need_put)
		c2_ref_put(&conn->nc_refs);

	return 0;
}


void c2_net_conn_init()
{
	c2_list_init(&conn_list);
	c2_rwlock_init(&conn_list_lock);
}

void c2_net_conn_fini()
{
	c2_list_fini(&conn_list);
	c2_rwlock_fini(&conn_list_lock);
}
