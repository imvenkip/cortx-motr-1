/* -*- C -*- */

#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>

#include "lib/cdefs.h"
#include "lib/cc.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/c2list.h"
#include "lib/queue.h"
#include "lib/thread.h"
#include "lib/cond.h"

#include "net/net.h"
#include "sunrpc.h"

/**
   @addtogroup sunrpc Sun RPC
   @{
 */

static const struct c2_net_conn_ops sunrpc_conn_ops;
static const struct c2_service_id_ops sunrpc_service_id_ops;
static const struct c2_net_xprt_ops xprt_ops;

enum {
	CONN_CLIENT_COUNT  = 8,
	CONN_CLIENT_THR_NR = CONN_CLIENT_COUNT * 2
};

/**
   Connection data private to sunrpc transport.

   @see c2_net_conn
 */
struct sunrpc_conn {
	/** Pool of sunrpc connections. */
	struct sunrpc_xprt *nsc_pool;
	/** Number of elements in the pool */
	size_t              nsc_nr;
	struct c2_mutex     nsc_guard;
	struct c2_queue     nsc_idle;
	struct c2_cond      nsc_gotfree;
};

struct sunrpc_xprt {
	CLIENT              *nsx_client;
	int                  nsx_fd;
	struct c2_queue_link nsx_linkage;
};

struct sunrpc_dom {
	bool             sd_shutown;
	struct c2_cond   sd_gotwork;
	struct c2_mutex  sd_guard;
	struct c2_queue  sd_queue;
	struct c2_thread sd_workers[CONN_CLIENT_THR_NR];
};

static bool dom_is_shutting(const struct c2_net_domain *dom)
{
	struct sunrpc_dom *xdom;

	xdom = dom->nd_xprt_private;
	return xdom->sd_shutown;
}

static void conn_fini_internal(struct sunrpc_conn *xconn)
{
	size_t i;

	if (xconn == NULL)
		return;

	if (xconn->nsc_pool != NULL) {
		for (i = 0; i < CONN_CLIENT_COUNT; ++i) {
			/* assume stdin is never used as a transport
			   socket. :-) */
			if (xconn->nsc_pool[i].nsx_fd != 0)
				/* rpc library closes the socket for us */
				clnt_destroy(xconn->nsc_pool[i].nsx_client);
		}
		c2_free(xconn->nsc_pool);
	}
	c2_free(xconn);
}

static int conn_init_one(struct c2_sunrpc_service_id *id, 
			 struct sunrpc_conn *xconn, struct sunrpc_xprt *xprt)
{
	struct sockaddr_in addr;
	int                result;
	int                sock;

	memset(&addr, 0, sizeof addr);
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = inet_addr(id->ssi_host);
	addr.sin_port        = htons(id->ssi_port);

	sock = -1;
	xprt->nsx_client = clnttcp_create(&addr, C2_SESSION_PROGRAM, 
					  C2_DEF_RPC_VER, &sock, 0, 0);
	if (xprt->nsx_client != NULL) {
		xprt->nsx_fd = sock;
		c2_queue_put(&xconn->nsc_idle, &xprt->nsx_linkage);
		result = 0;
	} else {
		clnt_pcreateerror(id->ssi_host);
		result = -errno;
	}
	return result;
}

static int sunrpc_conn_init(struct c2_service_id *id, struct c2_net_conn *conn)
{
	int                 result;
	struct sunrpc_conn *xconn;
	struct sunrpc_xprt *pool;

	if (dom_is_shutting(conn->nc_domain))
		return -ESHUTDOWN;

	result = -ENOMEM;
	C2_ALLOC_PTR(xconn);
	C2_ALLOC_ARR(pool, CONN_CLIENT_COUNT);
	if (xconn != NULL) {
		if (pool != NULL) {
			size_t i;

			c2_mutex_init(&xconn->nsc_guard);
			c2_queue_init(&xconn->nsc_idle);
			c2_cond_init(&xconn->nsc_gotfree);
			xconn->nsc_pool = pool;
			xconn->nsc_nr   = CONN_CLIENT_COUNT;
			conn->nc_ops    = &sunrpc_conn_ops;
			for (i = 0; i < CONN_CLIENT_COUNT; ++i) {
				result = conn_init_one(id->si_xport_private, 
						       xconn,
						       &xconn->nsc_pool[i]);
				if (result != 0)
					break;
			}
		}
	} else
		c2_free(pool);
	if (result != 0)
		conn_fini_internal(xconn);
	return result;
}

static void sunrpc_conn_fini(struct c2_net_conn *conn)
{
	conn_fini_internal(conn->nc_xprt_private);
}

static struct sunrpc_xprt *conn_xprt_get(struct sunrpc_conn *xconn)
{
	struct sunrpc_xprt *xprt;

	c2_mutex_lock(&xconn->nsc_guard);
	while (c2_queue_is_empty(&xconn->nsc_idle))
		c2_cond_wait(&xconn->nsc_gotfree, &xconn->nsc_guard);
	xprt = container_of(c2_queue_get(&xconn->nsc_idle),
			    struct sunrpc_xprt, nsx_linkage);
	c2_mutex_unlock(&xconn->nsc_guard);
	return xprt;
}

static void conn_xprt_put(struct sunrpc_conn *xconn, struct sunrpc_xprt *xprt)
{
	c2_mutex_lock(&xconn->nsc_guard);
	c2_queue_put(&xconn->nsc_idle, &xprt->nsx_linkage);
	c2_cond_signal(&xconn->nsc_gotfree);
	c2_mutex_unlock(&xconn->nsc_guard);
}

/* XXX Default timeout - need to be move in connection */
static struct timeval TIMEOUT = { .tv_sec = 5, .tv_usec = 0 };

static int sunrpc_conn_call(struct c2_net_conn *conn, 
			    const struct c2_rpc_op *op, void *arg, void *ret)
{
	struct sunrpc_conn *xconn;
	struct sunrpc_xprt *xprt;
	int                 result;

	C2_ASSERT(!dom_is_shutting(conn->nc_domain));

	xconn = conn->nc_xprt_private;
	xprt = conn_xprt_get(xconn);
	result = clnt_call(xprt->nsx_client, op->ro_op,
			   (xdrproc_t) op->ro_xdr_arg, (caddr_t) arg,
			   (xdrproc_t) op->ro_xdr_result, (caddr_t) ret,
			   TIMEOUT);
	conn_xprt_put(xconn, xprt);
	return result;
}

static int sunrpc_conn_send(struct c2_net_conn *conn, 
			    struct c2_net_async_call *call)
{
	struct sunrpc_dom *xdom;

	C2_ASSERT(!dom_is_shutting(conn->nc_domain));

	call->ac_conn = conn;
	xdom = conn->nc_domain->nd_xprt_private;

	c2_mutex_lock(&xdom->sd_guard);
	c2_queue_put(&xdom->sd_queue, &call->ac_linkage);
	c2_cond_signal(&xdom->sd_gotwork);
	c2_mutex_unlock(&xdom->sd_guard);
	
	return 0;
}

static void sunrpc_service_id_fini(struct c2_service_id *id)
{
	if (id->si_xport_private != NULL) {
		c2_free(id->si_xport_private);
		id->si_xport_private = NULL;
	}
}

static int sunrpc_service_id_init(struct c2_net_domain *dom,
				  struct c2_service_id *sid, va_list varargs)
{
	struct c2_sunrpc_service_id *xsid;
	int                          result;

	C2_ALLOC_PTR(xsid);
	if (xsid != NULL) {
		sid->si_xport_private = xsid;
		xsid->ssi_id = sid;
		xsid->ssi_host = va_arg(varargs, char *);
		xsid->ssi_port = va_arg(varargs, int);
		sid->si_ops = &sunrpc_service_id_ops;
		result = 0;
	} else
		result = -ENOMEM;
	return result;
}

static void sunrpc_worker(struct c2_net_domain *dom)
{
	struct sunrpc_dom        *xdom;
	struct c2_net_async_call *call;
	struct sunrpc_conn       *xconn;
	struct sunrpc_xprt       *xprt;
	const struct c2_rpc_op   *op;

	xdom = dom->nd_xprt_private;
	c2_mutex_lock(&xdom->sd_guard);
	while (1) {
		while (!xdom->sd_shutown && c2_queue_is_empty(&xdom->sd_queue))
			c2_cond_wait(&xdom->sd_gotwork, &xdom->sd_guard);
		if (xdom->sd_shutown)
			break;
		call = container_of(c2_queue_get(&xdom->sd_queue),
				    struct c2_net_async_call, ac_linkage);
		c2_mutex_unlock(&xdom->sd_guard);

		xconn = call->ac_conn->nc_xprt_private;
		xprt  = conn_xprt_get(xconn);
		op    = call->ac_op;
		call->ac_rc = clnt_call(xprt->nsx_client, op->ro_op,
					(xdrproc_t) op->ro_xdr_arg, 
					(caddr_t) call->ac_arg,
					(xdrproc_t) op->ro_xdr_result, 
					(caddr_t) call->ac_ret, TIMEOUT);
		c2_chan_broadcast(&call->ac_chan);
		conn_xprt_put(xconn, xprt);
		c2_mutex_lock(&xdom->sd_guard);
	}
	c2_mutex_unlock(&xdom->sd_guard);
}

static void dom_fini(struct c2_net_domain *dom)
{
	struct sunrpc_dom  *xdom;

	C2_ASSERT(c2_list_is_empty(&dom->nd_conn));
	C2_ASSERT(c2_list_is_empty(&dom->nd_service));

	xdom = dom->nd_xprt_private;
	if (xdom != NULL) {
		int i;

		xdom->sd_shutown = true;
		/* wake up all worker threads so that they detect shutdown and
		   exit */
		c2_cond_broadcast(&xdom->sd_gotwork);
		for (i = 0; i < ARRAY_SIZE(xdom->sd_workers); ++i) {
			if (xdom->sd_workers[i].t_func != NULL) {
				int rc;

				rc = c2_thread_join(&xdom->sd_workers[i]);
				/* XXX handle error... */
			}
		}
		c2_mutex_fini(&xdom->sd_guard);
		c2_queue_fini(&xdom->sd_queue);
		c2_cond_fini(&xdom->sd_gotwork);
		c2_free(xdom);
		dom->nd_xprt_private = NULL;
	}
}

static int dom_init(struct c2_net_xprt *xprt, struct c2_net_domain *dom)
{
	int i;
	int result;
	struct sunrpc_dom *xdom;

	C2_ALLOC_PTR(xdom);
	if (xdom != NULL) {
		dom->nd_xprt_private = xdom;
		c2_cond_init(&xdom->sd_gotwork);
		c2_mutex_init(&xdom->sd_guard);
		c2_queue_init(&xdom->sd_queue);
		for (i = 0; i < ARRAY_SIZE(xdom->sd_workers); ++i) {
			result = C2_THREAD_INIT(&xdom->sd_workers[i], 
						struct c2_net_domain *, 
						&sunrpc_worker, dom);
			if (result != 0)
				break;
		}
	} else
		result = -ENOMEM;
	if (result != 0)
		dom_fini(dom);
	return result;

}

static const struct c2_service_id_ops sunrpc_service_id_ops = {
	.sis_conn_init = sunrpc_conn_init,
	.sis_fini      = sunrpc_service_id_fini
};

static const struct c2_net_conn_ops sunrpc_conn_ops = {
	.sio_fini = sunrpc_conn_fini,
	.sio_call = sunrpc_conn_call,
	.sio_send = sunrpc_conn_send
};

static const struct c2_net_xprt_ops xprt_ops = {
	.xo_dom_init        = dom_init,
	.xo_dom_fini        = dom_fini,
	.xo_service_id_init = sunrpc_service_id_init
};

struct c2_net_xprt c2_net_sunrpc_xprt = {
	.nx_name = "sunrpc/user",
	.nx_ops  = &xprt_ops
};

/** @} end of group sunrpc */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
