/* -*- C -*- */
#ifndef __COLIBRI_NET_TYPES_H__

#define __COLIBRI_NET_TYPES_H__

#include "lib/cdefs.h"
#include "lib/c2list.h"
#include "lib/refs.h"

struct c2_net_xprt;
struct c2_net_xprt_ops;
struct c2_net_domain;
struct c2_service_id;
struct c2_service_id_ops;
struct c2_net_conn;
struct c2_net_conn_ops;
struct c2_service;
struct c2_service_ops;

/** network transport (e.g., lnet or sunrpc) */
struct c2_net_xprt {
	const char                   *nx_name;
	const struct c2_net_xprt_ops *nx_ops;
};

struct c2_net_xprt_ops {
	int  (*xo_dom_init)(struct c2_net_xprt *xprt, 
			    struct c2_net_domain *dom);
	void (*xo_dom_fini)(struct c2_net_domain *dom);
};

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

struct c2_service_id_ops {
	void (*sio_fini)(struct c2_net_conn *conn);
	int  (*sio_call)(struct c2_net_conn *conn, const struct c2_rpc_op *op,
			 void *arg, void *ret);
	int  (*sio_send)(struct c2_net_conn *conn, struct c2_net_async_call *c);
};


/**
 generic hanlder for XDR transformations

 @param x pointer to XDR object
 @param data pointer to memory region which consist data to store inside XDR,
	     or have enough memory to extract network data from XDR.

 @retval true iif conversion finished OK
 @retval false at other cases
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
const struct c2_rpc_op *c2_rpc_op_find(struct c2_rpc_op_table *ops, int op);

/* __COLIBRI_NET_TYPES_H__ */
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
