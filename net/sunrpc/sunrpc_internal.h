/* -*- C -*- */
#ifndef __COLIBRI_NET_SUNRPC_SUNRPC_INTERNAL_H__
#define __COLIBRI_NET_SUNRPC_SUNRPC_INTERNAL_H__

#include <lib/cdefs.h>

enum {
	USER_CONN_CLIENT_COUNT  = 8,
	USER_CONN_CLIENT_THR_NR = USER_CONN_CLIENT_COUNT * 2,
};

struct sunrpc_dom {
	bool             sd_shutown;
	/*
	 * Userspace llient side domain state.
	 */

	struct c2_cond   sd_gotwork;
	struct c2_mutex  sd_guard;
	struct c2_queue  sd_queue;
	struct c2_thread sd_workers[USER_CONN_CLIENT_THR_NR];

	/*
	 * Userspace server side domain state.
	 */

	/*
	 * kernelspace client side domain state.
	 */
};

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


/**
   SUNRPC service identifier.
 */
struct sunrpc_service_id {
	struct c2_service_id *ssi_id;
	char                 *ssi_host;	    /**< server hostname */
	struct sockadd_in    *ssi_sockaddr; /**< server ip_addr  */
	int 	              ssi_addrlen;  /**< server ip_addr  */
	uint16_t              ssi_port;     /**< server tcp port */
};

static inline bool dom_is_shutting(const struct c2_net_domain *dom)
{
	struct sunrpc_dom *xdom;

	xdom = dom->nd_xprt_private;
	return xdom->sd_shutown;
}



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
