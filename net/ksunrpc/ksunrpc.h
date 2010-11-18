/* -*- C -*- */
#ifndef __COLIBRI_NET_KSUNRPC_KSUNRPC_H__
#define __COLIBRI_NET_KSUNRPC_KSUNRPC_H__

#include <linux/in.h>
#include <linux/sunrpc/sched.h> /* for rpc_call_ops */

#include "lib/mutex.h"

struct c2_fop;

/**
   SUNRPC service identifier.
 */
struct ksunrpc_service_id {
	char                 ssi_host[256];/**< server hostname */
	struct sockaddr_in   ssi_sockaddr; /**< server ip_addr  */
	int 	             ssi_addrlen;  /**< server ip_addr  */
	uint16_t             ssi_port;     /**< server tcp port */
};

struct ksunrpc_xprt {
	struct rpc_clnt *ksx_client;
	struct c2_mutex  ksx_lock;
};

/**
   Service call description.
 */
struct c2_knet_call {
	/** Argument. */
	struct c2_fop          *ac_arg;
	/** Result, only meaningful when c2_net_async_call::ac_rc is 0. */
	struct c2_fop          *ac_ret;
};

struct ksunrpc_xprt_ops {
	/**
	   Initialise transport resources.
	 */
	struct ksunrpc_xprt* (*ksxo_init)(struct ksunrpc_service_id *xsid);

	/**
	   Finalise transport resources.
	 */
	void (*ksxo_fini)(struct ksunrpc_xprt *xprt);

	/**
	   Synchronously call operation on the target service and wait for
	   reply.
	 */
	int (*ksxo_call)(struct ksunrpc_xprt *xprt, struct c2_knet_call *kcall);
};

extern struct ksunrpc_xprt_ops ksunrpc_xprt_ops;

int c2_kcall_enc(void *rqstp, __be32 *data, struct c2_knet_call *kcall);
int c2_kcall_dec(void *rqstp, __be32 *data, struct c2_knet_call *kcall);

/* __COLIBRI_NET_KSUNRPC_KSUNRPC_H__ */
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
