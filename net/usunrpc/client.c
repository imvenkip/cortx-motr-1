/* -*- C -*- */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#include <arpa/inet.h>
#include <netdb.h>
#include <rpc/rpc.h>

#include "lib/errno.h"
#include "lib/cdefs.h"
#include "lib/rwlock.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/list.h"
#include "lib/queue.h"
#include "lib/thread.h"
#include "lib/cond.h"
#include "net/net.h"
#include "fop/fop.h"

#include "usunrpc.h"
#include "usunrpc_internal.h"

/**
   @addtogroup usunrpc User Level Sun RPC
   @{
 */

/*
 * Client code.
 */

/**
   XXX make version for all sun rpc calls to be const
 */
static const int C2_DEF_RPC_VER = 1;

/**
   services unique identifier
 */
enum c2_rpc_service_id {
	C2_SESSION_PROGRAM = 0x20000001
};

struct usunrpc_xprt {
	CLIENT              *nsx_client;
	int                  nsx_fd;
	struct c2_queue_link nsx_linkage;
};

/**
   Connection data private to sunrpc transport.

   Each usunroc connection comes with a pool of sunrpc library connections. The
   pool is used to emulate asynchronous interface on top of synchronous sunrpc
   library interfaces (but see the note about zero-duration timeout).

   @see c2_net_conn
*/
struct usunrpc_conn {
	/** Pool of sunrpc connections. */
	struct usunrpc_xprt *nsc_pool;
	/** Number of elements in the pool */
	size_t               nsc_nr;
	/** Mutex protecting connection fields */
	struct c2_mutex	     nsc_guard;
	/** Queue of not used pool elements */
	struct c2_queue      nsc_idle;
	/** Condition variable signalled when usunrpc_conn::nsc_idle becomes
	    non-empty */
	struct c2_cond	     nsc_gotfree;
};

static const struct c2_addb_loc usunrpc_addb_client = {
	.al_name = "usunrpc-client"
};

#define ADDB_ADD(conn, ev, ...) \
C2_ADDB_ADD(&(conn)->nc_addb, &usunrpc_addb_client, ev , ## __VA_ARGS__)

#define ADDB_CALL(conn, name, rc)					\
C2_ADDB_ADD(&(conn)->nc_addb, &usunrpc_addb_client,                     \
            c2_addb_func_fail, (name), (rc))

static void usunrpc_conn_fini_internal(struct usunrpc_conn *xconn)
{
	size_t i;

	if (xconn == NULL)
		return;

	if (xconn->nsc_pool != NULL) {
		for (i = 0; i < USUNRPC_CONN_CLIENT_COUNT; ++i) {
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

static int usunrpc_conn_init_one(struct usunrpc_service_id *id,
				 struct c2_net_conn *conn,
				 struct usunrpc_conn *xconn,
				 struct usunrpc_xprt *xprt)
{
	struct sockaddr_in addr;
	int                result;
	int                sock;
	struct hostent    *hp;

	memset(&addr, 0, sizeof addr);
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = inet_addr(id->ssi_host);
	addr.sin_port        = htons(id->ssi_port);

	if (inet_aton(id->ssi_host, &addr.sin_addr) == 0) {
		if ((hp = gethostbyname(id->ssi_host)) == NULL) {
			fprintf(stderr, "can't get address for %s\n",
				id->ssi_host);
			ADDB_CALL(conn, "gethostbyname", 0);
			return -1;
		}
		if (hp->h_length > sizeof(struct in_addr)) {
			ADDB_CALL(conn, "gethostbyname_len", hp->h_length);
			hp->h_length = sizeof(struct in_addr);
		}
		memcpy(&addr.sin_addr, hp->h_addr, hp->h_length);
	}

	sock = -1;
	xprt->nsx_client = clnttcp_create(&addr, id->ssi_prog, 
					  id->ssi_ver, &sock, 0, 0);
	if (xprt->nsx_client != NULL) {
		xprt->nsx_fd = sock;
		c2_queue_put(&xconn->nsc_idle, &xprt->nsx_linkage);
		result = 0;
	} else {
		clnt_pcreateerror(id->ssi_host);
		ADDB_CALL(conn, "clnttcp_create", -errno);
		result = -errno;
	}
	return result;
}

static int usunrpc_conn_init(struct c2_service_id *id, struct c2_net_conn *conn)
{
	int                 result;
	struct usunrpc_conn *xconn;
	struct usunrpc_xprt *pool;

	if (dom_is_shutting(conn->nc_domain))
		return -ESHUTDOWN;

	result = -ENOMEM;
	C2_ALLOC_PTR(xconn);

	if (xconn != NULL) {
		C2_ALLOC_ARR(pool, USUNRPC_CONN_CLIENT_COUNT);
		if (pool != NULL) {
			size_t i;

			c2_mutex_init(&xconn->nsc_guard);
			c2_queue_init(&xconn->nsc_idle);
			c2_cond_init(&xconn->nsc_gotfree);
			xconn->nsc_pool       = pool;
			xconn->nsc_nr         = USUNRPC_CONN_CLIENT_COUNT;
			conn->nc_ops          = &usunrpc_conn_ops;
			conn->nc_xprt_private = xconn;

			for (i = 0; i < USUNRPC_CONN_CLIENT_COUNT; ++i) {
				result = usunrpc_conn_init_one
					(id->si_xport_private, conn, xconn,
					 &xconn->nsc_pool[i]);
				if (result != 0)
					break;
			}
		}
	} else
		ADDB_ADD(conn, c2_addb_oom);
	if (result != 0) {
		/* xconn & pool will be released there */
		usunrpc_conn_fini_internal(xconn);
	}

	return result;
}

static void usunrpc_conn_fini(struct c2_net_conn *conn)
{
	usunrpc_conn_fini_internal(conn->nc_xprt_private);
}

static struct usunrpc_xprt *conn_xprt_get(struct usunrpc_conn *xconn)
{
	struct usunrpc_xprt *xprt;

	c2_mutex_lock(&xconn->nsc_guard);
	while (c2_queue_is_empty(&xconn->nsc_idle))
		c2_cond_wait(&xconn->nsc_gotfree, &xconn->nsc_guard);
	xprt = container_of(c2_queue_get(&xconn->nsc_idle),
			    struct usunrpc_xprt, nsx_linkage);
	c2_mutex_unlock(&xconn->nsc_guard);
	return xprt;
}

static void conn_xprt_put(struct usunrpc_conn *xconn, struct usunrpc_xprt *xprt)
{
	c2_mutex_lock(&xconn->nsc_guard);
	c2_queue_put(&xconn->nsc_idle, &xprt->nsx_linkage);
	c2_cond_signal(&xconn->nsc_gotfree, &xconn->nsc_guard);
	c2_mutex_unlock(&xconn->nsc_guard);
}

/* XXX Default timeout - need to be move in connection */
static struct timeval TIMEOUT = { .tv_sec = 300, .tv_usec = 0 };

static int usunrpc_call(struct usunrpc_xprt *xprt, struct c2_net_call *call)
{
	struct c2_fop *arg;
	struct c2_fop *ret;

	arg = call->ac_arg;
	ret = call->ac_ret;
	return -clnt_call(xprt->nsx_client, arg->f_type->ft_code,
			  (xdrproc_t)&c2_fop_uxdrproc, (caddr_t)arg,
			  (xdrproc_t)&c2_fop_uxdrproc, (caddr_t)ret, TIMEOUT);
}

static int usunrpc_conn_call(struct c2_net_conn *conn, struct c2_net_call *call)
{
	struct usunrpc_conn *xconn;
	struct usunrpc_xprt *xprt;
	int                  result;

	C2_ASSERT(!dom_is_shutting(conn->nc_domain));

	xconn  = conn->nc_xprt_private;
	xprt   = conn_xprt_get(xconn);
	result = usunrpc_call(xprt, call);
	conn_xprt_put(xconn, xprt);
	return result;
}

static int usunrpc_conn_send(struct c2_net_conn *conn, struct c2_net_call *call)
{
	struct usunrpc_dom *xdom;

	C2_ASSERT(!dom_is_shutting(conn->nc_domain));

	call->ac_conn = conn;
	xdom = conn->nc_domain->nd_xprt_private;

	c2_mutex_lock(&xdom->sd_guard);
	c2_queue_put(&xdom->sd_queue, &call->ac_linkage);
	c2_cond_signal(&xdom->sd_gotwork, &xdom->sd_guard);
	c2_mutex_unlock(&xdom->sd_guard);
	
	return 0;
}

static void usunrpc_service_id_fini(struct c2_service_id *id)
{
	if (id->si_xport_private != NULL) {
		c2_free(id->si_xport_private);
		id->si_xport_private = NULL;
	}
}

int usunrpc_service_id_init(struct c2_service_id *sid, va_list varargs)
{
	struct usunrpc_service_id *xsid;
	int                        result;

	C2_ALLOC_PTR(xsid);
	if (xsid != NULL) {
		sid->si_xport_private = xsid;
		xsid->ssi_id = sid;

		/* N.B. they have different order than kernelspace's ones */
		xsid->ssi_host = va_arg(varargs, char *);
		xsid->ssi_port = va_arg(varargs, int);
		xsid->ssi_prog = C2_SESSION_PROGRAM;
		xsid->ssi_ver  = C2_DEF_RPC_VER;
		sid->si_ops = &usunrpc_service_id_ops;
		result = 0;
	} else
		result = -ENOMEM;
	return result;
}

void usunrpc_client_worker(struct c2_net_domain *dom)
{
	struct usunrpc_dom        *xdom;
	struct c2_net_call        *call;
	struct usunrpc_conn       *xconn;
	struct usunrpc_xprt       *xprt;

	xdom = dom->nd_xprt_private;
	c2_mutex_lock(&xdom->sd_guard);
	while (1) {
		while (!xdom->sd_shutown && c2_queue_is_empty(&xdom->sd_queue))
			c2_cond_wait(&xdom->sd_gotwork, &xdom->sd_guard);
		if (xdom->sd_shutown)
			break;
		call = container_of(c2_queue_get(&xdom->sd_queue),
				    struct c2_net_call, ac_linkage);
		c2_mutex_unlock(&xdom->sd_guard);

		xconn = call->ac_conn->nc_xprt_private;
		xprt  = conn_xprt_get(xconn);
		call->ac_rc = usunrpc_call(xprt, call);
		c2_chan_broadcast(&call->ac_chan);
		conn_xprt_put(xconn, xprt);
		c2_mutex_lock(&xdom->sd_guard);
	}
	c2_mutex_unlock(&xdom->sd_guard);
}

const struct c2_service_id_ops usunrpc_service_id_ops = {
	.sis_conn_init = usunrpc_conn_init,
	.sis_fini      = usunrpc_service_id_fini
};

const struct c2_net_conn_ops usunrpc_conn_ops = {
	.sio_fini = usunrpc_conn_fini,
	.sio_call = usunrpc_conn_call,
	.sio_send = usunrpc_conn_send
};

/** @} end of group usunrpc */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
