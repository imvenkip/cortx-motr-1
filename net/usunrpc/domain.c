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
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/svc.h>
#include <pthread.h>  /* pthread_key */
#include <unistd.h>    /* close() */

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

#include "usunrpc_internal.h"

/**
   @addtogroup usunrpc User Level Sun RPC
   @{
 */
/* 
 * Maximum bulk IO size
 */
#define USUNRPC_MAX_BRW_SIZE (4 << 20)

/*
 * Domain code.
 */

static void usunrpc_dom_fini(struct c2_net_domain *dom)
{
	struct usunrpc_dom  *xdom;

	C2_ASSERT(c2_list_is_empty(&dom->nd_conn));
	C2_ASSERT(c2_list_is_empty(&dom->nd_service));

	xdom = dom->nd_xprt_private;
	if (xdom != NULL) {
		int i;

		xdom->sd_shutown = true;
		/* wake up all worker threads so that they detect shutdown and
		   exit */
		c2_mutex_lock(&xdom->sd_guard);
		c2_cond_broadcast(&xdom->sd_gotwork, &xdom->sd_guard);
		c2_mutex_unlock(&xdom->sd_guard);
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

static int usunrpc_dom_init(struct c2_net_xprt *xprt, struct c2_net_domain *dom)
{
	int i;
	int result;
	struct usunrpc_dom *xdom;

	C2_ALLOC_PTR(xdom);
	if (xdom != NULL) {
		dom->nd_xprt_private = xdom;
		c2_cond_init(&xdom->sd_gotwork);
		c2_mutex_init(&xdom->sd_guard);
		c2_queue_init(&xdom->sd_queue);

		for (i = 0; i < ARRAY_SIZE(xdom->sd_workers); ++i) {
			result = C2_THREAD_INIT(&xdom->sd_workers[i], 
						struct c2_net_domain *, 
						NULL,
						&usunrpc_client_worker,
						dom);
			if (result != 0)
				break;
		}
	} else
		result = -ENOMEM;
	if (result != 0)
		usunrpc_dom_fini(dom);
	return result;

}

static size_t usunrpc_net_bulk_size(void)
{
	return USUNRPC_MAX_BRW_SIZE;
}

static const struct c2_net_xprt_ops usunrpc_xprt_ops = {
	.xo_dom_init        = usunrpc_dom_init,
	.xo_dom_fini        = usunrpc_dom_fini,
	.xo_service_id_init = usunrpc_service_id_init,
	.xo_service_init    = usunrpc_service_init,
	.xo_net_bulk_size   = usunrpc_net_bulk_size
};

struct c2_net_xprt c2_net_usunrpc_xprt = {
	.nx_name = "sunrpc/user",
	.nx_ops  = &usunrpc_xprt_ops
};

int usunrpc_init(void)
{
	/* un-buffer to see error messages timely. */
        setbuf(stdout, NULL);
        setbuf(stderr, NULL);

	return usunrpc_server_init();
}

void usunrpc_fini(void)
{
	usunrpc_server_fini();
}

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
