/* -*- C -*- */
#ifndef __COLIBRI_NET_KSUNRPC_KSUNRPC_H__
#define __COLIBRI_NET_KSUNRPC_KSUNRPC_H__

#include "lib/mutex.h"
#include "net/net.h"

#ifdef __KERNEL__
#include <linux/in.h>
#include <linux/sunrpc/sched.h> /* for rpc_call_ops */

struct c2_fop;
struct ksunrpc_service_id;

struct ksunrpc_dom {
	/*
	 * Kernel space client side domain state.
	 */

	int kd_dummy; /**< not used */

	/*
	 * Userspace server side domain state.
	 */
};

struct c2_fop;


int ksunrpc_service_id_init(struct c2_service_id *sid, va_list varargs);
int ksunrpc_service_init(struct c2_service *service);

extern const struct c2_service_id_ops ksunrpc_service_id_ops;
extern const struct c2_net_conn_ops   ksunrpc_conn_ops;

struct ksunrpc_xprt {
	struct rpc_clnt *ksx_client;
	struct c2_mutex  ksx_lock;
};

/**
   Connection data private to sunrpc transport.

   @see c2_net_conn
*/
struct ksunrpc_conn {
	struct ksunrpc_xprt *ksc_xprt;
};

int c2_kcall_enc(void *rqstp, __be32 *data, struct c2_net_call *call);
int c2_kcall_dec(void *rqstp, __be32 *data, struct c2_net_call *call);
/* #else __KERNEL__ */
#else
# include <netinet/in.h>
/* #ifdef __KERNEL__ */
#endif

/**
   SUNRPC service identifier.
 */
struct ksunrpc_service_id {
	struct c2_service_id *ssi_id;
	char                  ssi_host[256];/**< server hostname */
	struct sockaddr_in    ssi_sockaddr; /**< server ip_addr  */
	int 	              ssi_addrlen;  /**< server ip_addr  */
	uint16_t              ssi_port;     /**< server tcp port */
	uint32_t              ssi_prog;     /**< server program number */
	uint32_t              ssi_ver;      /**< server program version */
};

int c2_ksunrpc_init(void);
void c2_ksunrpc_fini(void);

extern const struct c2_service_id_ops ksunrpc_service_id_ops;
extern const struct c2_net_conn_ops ksunrpc_conn_ops;

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
