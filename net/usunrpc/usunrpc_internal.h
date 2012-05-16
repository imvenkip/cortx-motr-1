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

#ifndef __COLIBRI_NET_SUNRPC_SUNRPC_INTERNAL_H__
#define __COLIBRI_NET_SUNRPC_SUNRPC_INTERNAL_H__

#include "lib/cdefs.h"

/**
   @addtogroup usunrpc User level Sun RPC

   User level Sunrpc-based implementation of C2 networking interfaces.

   The implementation uses public sunrpc interfaces (rpc/rpc.h, part of libc on
   Linux). The following C2 network properties are not entirely trivial in
   sunrpc model:

   @li asynchronous client calls;

   @li one-way messages (rpcs without replies, the replies are sent as another
   one way rpc in the opposite direction);

   @li multi-threaded server.

   Currently asynchronous client calls are implemented by creating a pool of
   sunrpc client connections (CLIENT structure) per C2 logical network
   connection. To submit an asynchronous call, a user allocates a call structure
   (c2_net_call) and places it in a global (per network domain) queue.

   A global (per domain) pool of threads listens on this queue and processes
   requests synchronously.

   One-way messaging is not implemented at the moment.

   Multi-threaded server (described in detail in sunrpc_service documentation)
   uses select(2) call to detect incoming data.

   @todo A nicer approach to implement both asynchronous messaging and one-way
   messaging is to use zero-duration timeouts for clnt_call(3).

   @{
*/

enum {
	USUNRPC_CONN_CLIENT_COUNT  = 8,
	USUNRPC_CONN_CLIENT_THR_NR = USUNRPC_CONN_CLIENT_COUNT * 2,
};

struct usunrpc_dom {
	bool             sd_shutown;
	/*
	 * Userspace client side domain state.
	 */

	struct c2_cond   sd_gotwork;
	struct c2_mutex  sd_guard;
	struct c2_queue  sd_queue;
	int              sd_client_count;
	int              sd_nr_workers;
	struct c2_thread sd_workers[USUNRPC_CONN_CLIENT_THR_NR];

	/*
	 * Userspace server side domain state.
	 */
};

static inline bool udom_is_shutting(const struct c2_net_domain *dom)
{
	return ((struct usunrpc_dom *)dom->nd_xprt_private)->sd_shutown;
}

/**
   SUNRPC service identifier.
 */
struct usunrpc_service_id {
	struct c2_service_id *ssi_id;
	char                  ssi_host[256];/**< server hostname */
	struct sockadd_in    *ssi_sockaddr; /**< server ip_addr  */
	int 	              ssi_addrlen;  /**< server ip_addr  */
	uint16_t              ssi_port;     /**< server tcp port */
	uint32_t              ssi_prog;     /**< server program number */
	uint32_t              ssi_ver;      /**< server program version */
};

int  usunrpc_server_init(void);
void usunrpc_server_fini(void);

void usunrpc_client_worker(struct c2_net_domain *dom);
int  usunrpc_service_id_init(struct c2_service_id *sid, va_list varargs);
int  usunrpc_service_init(struct c2_service *service);

extern const struct c2_service_id_ops usunrpc_service_id_ops;
extern const struct c2_net_conn_ops usunrpc_conn_ops;
extern const struct c2_service_ops usunrpc_service_ops;
extern struct c2_net_xprt c2_net_usunrpc_minimal_xprt;

/** @} end of group usunrpc */

/* __COLIBRI_NET_SUNRPC_SUNRPC_INTERNAL_H__ */
#endif
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
