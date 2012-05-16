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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 06/15/2010
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
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
#include "net/net_internal.h"

#include "usunrpc_internal.h"

/**
   @addtogroup usunrpc User Level Sun RPC
   @{
 */

/*
 * Maximum bulk IO size
 */
enum {
	USUNRPC_MAX_BRW_SIZE = (4 << 20)
};

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
		for (i = 0; i < xdom->sd_nr_workers; ++i) {
			if (xdom->sd_workers[i].t_func != NULL)
				c2_thread_join(&xdom->sd_workers[i]);
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
		if (xprt == &c2_net_usunrpc_minimal_xprt) {
			xdom->sd_client_count = 4;
			xdom->sd_nr_workers = 0;
			result = 0;
		} else {
			xdom->sd_client_count = USUNRPC_CONN_CLIENT_COUNT;
			xdom->sd_nr_workers = USUNRPC_CONN_CLIENT_THR_NR;
			C2_ASSERT(ARRAY_SIZE(xdom->sd_workers) ==
				  xdom->sd_nr_workers);
		}

		for (i = 0; i < xdom->sd_nr_workers; ++i) {
			result = C2_THREAD_INIT(&xdom->sd_workers[i],
						struct c2_net_domain *,
						NULL,
						&usunrpc_client_worker,
						dom,
						"usunrpc_clnt%d", i);
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

/**
   Default version of the usunrpc transport with a total of 25 threads
   when run with a server:
   - 16 client threads with support for c2_net_client_send
   - 8 socket per client connection
   - 9 server threads (scheduler + 8 workers)
 */
struct c2_net_xprt c2_net_usunrpc_xprt = {
	.nx_name = "sunrpc/user",
	.nx_ops  = &usunrpc_xprt_ops
};

/**
   Minimal version of the usunrpc transport with a total of 4 threads
   when run with a server:
   - 0 client threads (i.e. no support for c2_net_client_send)
   - 4 sockets per client connection
   - 3 server threads (scheduler + 2 workers)
 */
struct c2_net_xprt c2_net_usunrpc_minimal_xprt = {
	.nx_name = "minimal-sunrpc/user",
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
