#include <errno.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>

#include "lib/cdefs.h"
#include "lib/cc.h"
#include "lib/c2list.h"
#include "lib/refs.h"
#include "lib/memory.h"

#include "net/net.h"
#include "net/net_types.h"
#include "net/connection.h"


static struct c2_rwlock conn_list_lock;
static struct c2_list   conn_list;

static void c2_net_conn_free_cb(struct c2_ref *ref)
{
	struct c2_net_conn *conn;

	conn = container_of(ref, struct c2_net_conn, nc_refs);

	clnt_destroy(conn->nc_cli);
	C2_FREE_PTR(conn);
}

int c2_net_conn_create(const struct c2_node_id *nid, const unsigned long prgid,
		       char *nn)
{
	struct c2_net_conn *conn;
	CLIENT *cli;

	C2_ALLOC_PTR(conn);
	if (conn == NULL)
		return -ENOMEM;

	/** XXX sun rpc */
	cli = clnt_create (nn, prgid, 1, "tcp");
	if (cli == NULL) {
		C2_FREE_PTR(conn);
		return -ENOTCONN;
	}

	c2_list_link_init(&conn->nc_link);
	conn->nc_id = *nid;
	conn->nc_prgid = prgid;
	c2_ref_init(&conn->nc_refs, 1, c2_net_conn_free_cb);
	conn->nc_cli = cli;

	c2_rwlock_write_lock(&conn_list_lock);
	c2_list_add(&conn_list, &conn->nc_link);
	c2_rwlock_write_unlock(&conn_list_lock);

	return 0;
}

struct c2_net_conn *c2_net_conn_find(const struct c2_node_id *nid)
{
	struct c2_net_conn *conn;
	bool found = false;

	c2_rwlock_read_lock(&conn_list_lock);
	c2_list_for_each_entry(&conn_list, conn, struct c2_net_conn, nc_link) {
		if (c2_nodes_is_same(&conn->nc_id, nid)) {
			c2_ref_get(&conn->nc_refs);
			found = true;
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
	bool need_put = false;

	c2_rwlock_write_lock(&conn_list_lock);
	if (c2_list_link_is_in(&conn->nc_link)) {
		c2_list_del_init(&conn->nc_link);
		need_put = true;
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

