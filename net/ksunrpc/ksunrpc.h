/* -*- C -*- */
#ifndef __COLIBRI_NET_KSUNRPC_KSUNRPC_H__
#define __COLIBRI_NET_KSUNRPC_KSUNRPC_H__

/**
   SUNRPC service identifier.
 */
struct ksunrpc_service_id {
	char                 *ssi_host;	    /**< server hostname */
	struct sockaddr_in   *ssi_sockaddr; /**< server ip_addr  */
	int 	              ssi_addrlen;  /**< server ip_addr  */
	uint16_t              ssi_port;     /**< server tcp port */
};

struct ksunrpc_xprt {
	struct rpc_clnt *ksx_client;
};

struct c2_rpc_op;
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
	int (*ksxo_call)(struct ksunrpc_xprt *xprt, const struct c2_rpc_op *op,
			 void *arg, void *ret);

	/**
	   Post an asynchronous operation to the target service.

	   The completion is announced by signalling c2_net_async_call::ac_chan.
	 */
	int (*ksxo_send)(struct ksunrpc_xprt *xprt, const struct c2_rpc_op *op,
			 void *arg, void *ret, struct rpc_call_ops *async_ops,
			 void *data);
};

extern struct ksunrpc_xprt_ops ksunrpc_xprt_ops;

/*
 XXX The following should be identical to the definition in net/net.h
*/

typedef	int (*c2_xdrproc_t)(void *xdr, void *data);
typedef	int (*c2_rpc_srv_handler)(const struct c2_rpc_op *op,
				  void *arg, void *ret);
struct c2_rpc_op {
	/**
	 operation identifier
	 */
	uint64_t	ro_op;
	/**
	 size of incoming argument
	 */
	size_t		ro_arg_size;
	/**
	 XDR program to converting argument of remote procedure call
	 */
	c2_xdrproc_t	ro_xdr_arg;
	/**
	 size of reply
	 */
	size_t		ro_result_size;
	/**
	 XDR program to converting result of remote procedure call
	 */
	c2_xdrproc_t	ro_xdr_result;
	/**
	 function to a handle operation on server side
	 */
	c2_rpc_srv_handler ro_handler;
	char		  *ro_name;
};

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
