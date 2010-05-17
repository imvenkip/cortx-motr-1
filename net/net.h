/* -*- C -*- */
#ifndef __COLIBRI_NET_NET_H__

#define __COLIBRI_NET_NET_H__

#include <stdarg.h>

#include "lib/cdefs.h"
#include "lib/cc.h"
#include "lib/c2list.h"
#include "lib/queue.h"
#include "lib/refs.h"
#include "lib/chan.h"

/**
   @defgroup net Networking.
   @{
 */

struct c2_net_xprt;
struct c2_net_xprt_ops;
struct c2_net_domain;
struct c2_service_id;
struct c2_service_id_ops;
struct c2_net_conn;
struct c2_net_conn_ops;
struct c2_service;
struct c2_service_ops;
struct c2_rpc_op;
struct c2_net_async_call;

/** network transport (e.g., lnet or sunrpc) */
struct c2_net_xprt {
	const char                   *nx_name;
	const struct c2_net_xprt_ops *nx_ops;
};

struct c2_net_xprt_ops {
	int  (*xo_dom_init)(struct c2_net_xprt *xprt, 
			    struct c2_net_domain *dom);
	void (*xo_dom_fini)(struct c2_net_domain *dom);
	int  (*xo_service_id_init)(struct c2_net_domain *dom,
				   struct c2_service_id *sid, va_list varargs);
};

int  c2_net_xprt_init(struct c2_net_xprt *xprt);
void c2_net_xprt_fini(struct c2_net_xprt *xprt);

/**
   Collection of network resources.

   @todo for now assume that a domain is associated with single transport. In
   the future domains with connections and services over different transports
   will be supported.
 */
struct c2_net_domain {
	struct c2_rwlock    nd_lock;
	/** List of connections in this domain. */
	struct c2_list      nd_conn;
	/** List of services running in this domain. */
	struct c2_list      nd_service;
	/** Transport private domain data. */
	void               *nd_xprt_private;
	struct c2_net_xprt *nd_xprt;
};

/**
   Initialize resources for a domain.
 */
int c2_net_domain_init(struct c2_net_domain *dom, struct c2_net_xprt *xprt);

/**
   Release resources related to a domain.
 */
void c2_net_domain_fini(struct c2_net_domain *dom);


enum {
	C2_SERVICE_UUID_SIZE = 40
};

/**
   Unique service identifier.

   Each service has its own identifier. Different services running on
   the same node have different identifiers.

   An identifier contains enough information to locate a service in
   the cluster and to connect to it.
 */
struct c2_service_id {
	/** generic identifier */
	char                            si_uuid[C2_SERVICE_UUID_SIZE];
	/** a domain this service is addressed from */
	struct c2_net_domain           *si_domain;
	/** pointer to transport private service identifier */
	void                           *si_xport_private;
	const struct c2_service_id_ops *si_ops;
};

struct c2_service_id_ops {
	void (*sis_fini)(struct c2_service_id *id);
	int  (*sis_conn_init)(struct c2_service_id *id, struct c2_net_conn *c);
};

int  c2_service_id_init(struct c2_service_id *id, struct c2_net_domain *d, ...);
void c2_service_id_fini(struct c2_service_id *id);

/**
   Compare node identifiers for equality.
 */
bool c2_services_are_same(const struct c2_service_id *c1,
			  const struct c2_service_id *c2);


/**
   Running service instance.

   This structure describes an instance of a service running locally.
 */
struct c2_service {
	/** a domain this service is running at */
	struct c2_net_domain           *s_domain;
	/** linkage in the list of all services running in the domain */
	struct c2_list                  s_linkage;
	/** pointer to transport private service data */
	void                           *s_xport_private;
	const struct c2_service_ops    *s_ops;
};

/**
 services unique identifier
 */
enum c2_rpc_service_id {
	C2_SESSION_PROGRAM = 0x20000001
};

/**
 XXX make version for all sun rpc calls to be const
*/
static const int C2_DEF_RPC_VER = 1;

/**
   Client side of a logical network connection to a service.
 */
struct c2_net_conn {
	/** a domain this connection originates at */
	struct c2_net_domain         *nc_domain;
	/**
	 entry to linkage structure into connection list
	 */
	struct c2_list_link	      nc_link;
	/**
	   Service identifier.
	 */
	struct c2_service_id         *nc_id;
	/**
	 reference counter
	 */
	struct c2_ref		      nc_refs;
	/**
	   Pointer to transport private data.
	 */
	void                         *nc_xprt_private;
	const struct c2_net_conn_ops *nc_ops;
};

struct c2_net_conn_ops {
	void (*sio_fini)(struct c2_net_conn *conn);
	int  (*sio_call)(struct c2_net_conn *conn, const struct c2_rpc_op *op,
			 void *arg, void *ret);
	int  (*sio_send)(struct c2_net_conn *conn, struct c2_net_async_call *c);
};

/**
   Create network connection to a given service.

   Allocates resources and connects transport connection to some logical
   connection.  Logical connection is used to send rpc in the context of one or
   more sessions.  (@ref rpc-cli-session)

 @param nid - service identifier
 @param prgid program identifier, some unique identifier to identify service group.
 @param nn node name, currently host name which hold service with node id 'nid'

 @retval 0 is OK
 @retval <0 error is hit
*/
int c2_net_conn_create(struct c2_service_id *nid);

/**
 find a connection to a specified node.
 Scans the list of connections to find a logical connection associated with
 a given nid.

 @param nid service identifier
 @param prgid program identifier, some unique identifier to identify service group.

 @retval NULL if none connections to the node
 @retval !NULL connection info pointer
 */
struct c2_net_conn *c2_net_conn_find(const struct c2_service_id *nid);

/**
 release connection after using.
 function is release one reference from network connection.
 reference to transport connection is released if that is last reference
 to network connection

 @param conn pointer to network connection.

 @return none
*/
void c2_net_conn_release(struct c2_net_conn *conn);

/**
 unlink connection from connection list.
 transport connection(s) will released when last user of that connection 
 will release that logical connection.

 @param conn pointer to network connection.
 */
void c2_net_conn_unlink(struct c2_net_conn *conn);

/**
 generic hanlder for XDR transformations

 @param x pointer to XDR object
 @param data pointer to memory region which consist data to store inside XDR,
	     or have enough memory to extract network data from XDR.

 @retval true iif conversion finished OK
 */
typedef	bool (*c2_xdrproc_t)(void *xdr, void *data);

/**
 server side RPC handler.

 @param arg - incoming argument
 @param ret - pointer to memory area to store result. this area allocated
		before call the hanlder.

 @retval true if ret pointed to correct reply and can send over wire
 @retval false if ret consist invalid data and system error should be returned
	       to client
 */
typedef	bool (*c2_rpc_srv_handler) (void *arg, void *ret);

/**
   rpc commands associated with service thread
 */
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
};

/**
 we want to use same structure for build server and client code, bu
 if we build client code we can't have pointer to server side handler.
*/
#ifdef C2_RPC_CLIENT
#define C2_RPC_SRV_PROC(name)	(NULL)
#else
#define C2_RPC_SRV_PROC(name)	((c2_rpc_srv_handler)(name))
#endif

/**
 structre to hold an array of operations to handle in the service
 */
struct c2_rpc_op_table;

/**
 allocate new table and fill with initial values
 */
int c2_rpc_op_table_init(struct c2_rpc_op_table **table);

/**
 free allocated resources
 */
void c2_rpc_op_table_fini(struct c2_rpc_op_table *table);

/**
 register one rpc operation in the table
 */
int c2_rpc_op_register(struct c2_rpc_op_table *table, const struct c2_rpc_op *op);

/**
 find operation in table. function scanned table with operation to find
 requested operation.

 @param ops pointer filled operations table
 @param op rpc operation to find

 @retval NULL if pointer not exist or wrong parameters
 @retval !NULL if operations found in table
 */
const struct c2_rpc_op *c2_rpc_op_find(struct c2_rpc_op_table *ops, 
				       uint64_t op);

struct c2_rpc_op_table;

/**
 synchronous rpc call. client blocked until rpc finished.

 @param conn - network connection associated with replier
 @param rot - pointer to operations table associated with replier
 @param op - operation to call on replier
 @param arg - pointer to buffer with argument of operation
 @param ret - pointer to buffer to put reply from a replier

 @retval 0 OK
 @retval <0 ERROR
 */
int c2_net_cli_call(struct c2_net_conn *conn, struct c2_rpc_op_table *rot,
		    uint64_t op, void *arg, void *ret);

struct c2_net_async_call {
	struct c2_net_conn     *ac_conn;
	const struct c2_rpc_op *ac_op;
	void                   *ac_arg;
	void                   *ac_ret;
	uint32_t                ac_rc;
	struct c2_chan          ac_chan;
	struct c2_queue_link    ac_linkage;
};

/*
 asynchronous rpc call. client is continue after request queried
 to transfer.

 @param conn - network connection associated with replier
 @param rot - pointer to operations table associated with replier
 @param op - operation to call on replier
 @param arg - pointer to buffer with argument of operation
 @param ret - pointer to buffer to put reply from a replier
 @param cb - pointer to callback function called after hit a error in
	     in transfer, or reply is accepted
 @param datum - an argument to be passed to the call-back.

 @retval 0 OK
 @retval <0 ERROR
*/
int c2_net_cli_send(struct c2_net_conn *conn, struct c2_net_async_call *call);


/**
 initialize network service and attach incoming messages handler

 typical use is define custom handler and call svc_generic function with custom
 array of operations.

 @param id service identifier

 */
int c2_net_service_start(enum c2_rpc_service_id id, struct c2_rpc_op_table *ops,
			 struct c2_service *service);

int c2_net_service_stop(struct c2_service *service);

/**
 constructor for the network library
 */
int c2_net_init(void);

/**
 destructor for the network library.
 release all allocated resources
 */
void c2_net_fini(void);

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
