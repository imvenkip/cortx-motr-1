/* -*- C -*- */
#include <errno.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <lib/cdefs.h>
#include <lib/rwlock.h>
#include <lib/c2list.h>
#include <lib/refs.h>
#include <lib/memory.h>
#include "net.h"

static void c2_net_conn_free_cb(struct c2_ref *ref)
{
	struct c2_net_conn *conn;

	conn = container_of(ref, struct c2_net_conn, nc_refs);
	conn->nc_ops->sio_fini(conn);
	c2_free(conn);
}

int c2_net_conn_create(struct c2_service_id *nid)
{
	struct c2_net_conn   *conn;
	struct c2_net_domain *dom;
	int                   result;

	dom = nid->si_domain;
	C2_ALLOC_PTR(conn);
	if (conn != NULL) {
		conn->nc_domain = dom;
		result = nid->si_ops->sis_conn_init(nid, conn);
		if (result == 0) {
			c2_ref_init(&conn->nc_refs, 1, c2_net_conn_free_cb);
			conn->nc_id = nid;

			c2_rwlock_write_lock(&dom->nd_lock);
			c2_list_add(&dom->nd_conn, &conn->nc_link);
			c2_rwlock_write_unlock(&dom->nd_lock);
		} else
			c2_free(conn);
	} else
		result = -ENOMEM;
	return result;
}

struct c2_net_conn *c2_net_conn_find(const struct c2_service_id *nid)
{
	struct c2_net_conn   *conn;
	struct c2_net_domain *dom;
	bool                  found = false;

	dom = nid->si_domain;

	c2_rwlock_read_lock(&dom->nd_lock);
	c2_list_for_each_entry(&dom->nd_conn, conn, 
			       struct c2_net_conn, nc_link) {
		C2_ASSERT(conn->nc_domain == dom);
		if (c2_services_are_same(conn->nc_id, nid)) {
			c2_ref_get(&conn->nc_refs);
			found = true;
			break;
		}
	}
	c2_rwlock_read_unlock(&dom->nd_lock);

	return found ? conn : NULL;
}

void c2_net_conn_release(struct c2_net_conn *conn)
{
	c2_ref_put(&conn->nc_refs);
}

void c2_net_conn_unlink(struct c2_net_conn *conn)
{
	bool                  need_put = false;
	struct c2_net_domain *dom;

	dom = conn->nc_domain;

	c2_rwlock_write_lock(&dom->nd_lock);
	if (c2_list_link_is_in(&conn->nc_link)) {
		c2_list_del_init(&conn->nc_link);
		need_put = true;
	}
	c2_rwlock_write_unlock(&dom->nd_lock);

	if (need_put)
		c2_ref_put(&conn->nc_refs);
}

int c2_net_domain_init(struct c2_net_domain *dom, struct c2_net_xprt *xprt)
{
	c2_list_init(&dom->nd_conn);
	c2_list_init(&dom->nd_service);
	c2_rwlock_init(&dom->nd_lock);
	dom->nd_xprt = xprt;
	return xprt->nx_ops->xo_dom_init(xprt, dom);
}

void c2_net_domain_fini(struct c2_net_domain *dom)
{
	dom->nd_xprt->nx_ops->xo_dom_fini(dom);
	c2_rwlock_init(&dom->nd_lock);
	c2_list_init(&dom->nd_service);
	c2_list_init(&dom->nd_conn);
}

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
