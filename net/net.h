/* -*- C -*- */
#ifndef __COLIBRI_NET_NET_H__
#define __COLIBRI_NET_NET_H__

#include <stdarg.h>

#include "lib/cdefs.h"
#include "lib/rwlock.h"
#include "lib/list.h"
#include "lib/queue.h"
#include "lib/refs.h"
#include "lib/chan.h"
#include "lib/time.h"
#include "addb/addb.h"


/**
   @defgroup net Networking.

   Major data-types in C2 networking are:

   @li transport (c2_net_xprt);

   @li network domain (c2_net_domain);

   @li service id (c2_service_id);

   @li service (c2_service);

   @li logical connection (c2_net_conn).

   @{
 */

/*import */
struct c2_fop;

/* export */
struct c2_net_xprt;
struct c2_net_xprt_ops;
struct c2_net_domain;
struct c2_service_id;
struct c2_service_id_ops;
struct c2_net_conn;
struct c2_net_conn_ops;
struct c2_service;
struct c2_service_ops;
struct c2_net_call;

/** Network transport (e.g., lnet or sunrpc) */
struct c2_net_xprt {
	const char                   *nx_name;
	const struct c2_net_xprt_ops *nx_ops;
};

/** Network transport operations. */
struct c2_net_xprt_ops {
	/**
	   Initialise transport specific part of a domain (e.g., start threads,
	   initialise portals).
	 */
	int  (*xo_dom_init)(struct c2_net_xprt *xprt,
			    struct c2_net_domain *dom);
	/**
	   Finalise transport resources in a domain.
	 */
	void (*xo_dom_fini)(struct c2_net_domain *dom);
	/**
	   Initialise transport specific part of a service identifier.
	 */
	int  (*xo_service_id_init)(struct c2_service_id *sid, va_list varargs);

	/**
	   Initialise the server side part of a transport.
	 */
	int  (*xo_service_init)(struct c2_service *service);

	/**
	   Interface to return maxima for bulk I/O for network transport e.g.,
	   lnet or sunrpc.

	   The interface can be made generic enough to return any other property
	   of network transport.
	 */
	size_t (*xo_net_bulk_size)(void);
};

int  c2_net_xprt_init(struct c2_net_xprt *xprt);
void c2_net_xprt_fini(struct c2_net_xprt *xprt);

enum c2_net_stats_direction {
        NS_STATS_IN  = 0,
        NS_STATS_OUT = 1,
        NS_STATS_NR
};

struct c2_net_stats {
        struct c2_rwlock ns_lock;
        /**
         All counters are 64 bits wide and wrap naturally. We re-zero
         the counters every time we examine the stats so that we have a known
         timebase for rate calculations. */
        struct c2_time     ns_time;
        /** Counts how many FOPs have been seen by the service workers */
        struct c2_atomic64 ns_reqs;
        /** Bytes inside FOPs, as determined by fop type layout */
        struct c2_atomic64 ns_bytes;
        /**
         Counts how many times an idle thread is woken to try to
         receive some data from a transport.

         This statistic tracks the circumstance where incoming
         network-facing work is being handled quickly, which is a good
         thing.  The ideal rate of change for this counter will be close
         to but less than the rate of change of the ns_reqs counter.
         */
        struct c2_atomic64 ns_threads_woken;
        uint64_t           ns_max;      /**< Max load seen so far */
        bool               ns_got_busy; /**< We can believe max rate */
};

/**
   Collection of network resources.

   Network connections, service ids and services exist within a network domain.

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
        /** Domain network stats */
        struct c2_net_stats nd_stats[NS_STATS_NR];
	/**
	   ADDB context for events related to this domain
	 */
	struct c2_addb_ctx  nd_addb;

	/**
	   Lock to protect the addb items list.
	 */
	struct c2_rwlock    nd_addb_lock;
	/**
	   ADDB record items list.
	 */
	struct c2_list      nd_addb_items;
};

/**
   Initialize resources for a domain.
 */
int c2_net_domain_init(struct c2_net_domain *dom, struct c2_net_xprt *xprt);

/**
   Release resources related to a domain.
 */
void c2_net_domain_fini(struct c2_net_domain *dom);

void c2_net_domain_stats_init(struct c2_net_domain *dom);
void c2_net_domain_stats_fini(struct c2_net_domain *dom);

/**
 Collect values for stats.
 */
void c2_net_domain_stats_collect(struct c2_net_domain *dom,
                                 enum c2_net_stats_direction dir,
                                 uint64_t bytes,
                                 bool *sleeping);
/**
 Report the network loading rate for a direction (in/out).
 @returnval rate, in percent * 100 of maximum seen rate (e.g. 1234 = 12.34%)
 */
int c2_net_domain_stats_get(struct c2_net_domain *dom,
                            enum c2_net_stats_direction dir);


enum {
	C2_SERVICE_UUID_SIZE = 40
};

/**
   Unique service identifier.

   Each service has its own identifier. Different services running on
   the same node have different identifiers.

   An identifier contains enough information to locate a service in
   the cluster and to connect to it.

   A service identifier is used by service clients to open connections to the
   service.
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
	/** Finalise service identifier */
	void (*sis_fini)(struct c2_service_id *id);
	/** Initialise a connection to this service */
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
   Table of operations, supported by a service.

   Operations supported by a service are identified by a scalar "opcode". A
   server supports a continuous range of opcodes. This simple model simplifies
   memory management and eliminates a loop over an array of a list in a service
   hot path.
 */
struct c2_net_op_table {
	uint64_t             not_start;
	uint64_t             not_nr;
	struct c2_fop_type **not_fopt;
};

/**
   Running service instance.

   This structure describes an instance of a service running locally.
 */
struct c2_service {
	/** an identifier of this service */
	struct c2_service_id           *s_id;
	/** Domain this service is running in. */
	struct c2_net_domain           *s_domain;
	/** Table of operations. */
	struct c2_net_op_table          s_table;
	int                           (*s_handler)(struct c2_service *service,
						   struct c2_fop *fop,
						   void *cookie);
	/**
	    linkage in the list of all services running in the domain
	*/
	struct c2_list_link             s_linkage;
	/** pointer to transport private service data */
	void                           *s_xport_private;
	const struct c2_service_ops    *s_ops;
	struct c2_addb_ctx              s_addb;
};

struct c2_service_ops {
	void (*so_fini)(struct c2_service *service);
	void (*so_reply_post)(struct c2_service *service,
			      struct c2_fop *fop, void *cookie);
};

/**
   Client side of a logical network connection to a service.
 */
struct c2_net_conn {
	/**
	    A domain this connection originates at.
	 */
	struct c2_net_domain         *nc_domain;
	/**
	   Entry to linkage structure into connection list.
	 */
	struct c2_list_link	      nc_link;
	/**
	   Service identifier of the service this connection is to.
	 */
	struct c2_service_id         *nc_id;
	/**
	   Reference counter.
	 */
	struct c2_ref		      nc_refs;
	/**
	   Pointer to transport private data.
	 */
	void                         *nc_xprt_private;
	const struct c2_net_conn_ops *nc_ops;
	/**
	   ADDB context for events related to this connection.
	 */
	struct c2_addb_ctx            nc_addb;
};

struct c2_net_conn_ops {
	/**
	   Finalise connection resources.
	 */
	void (*sio_fini)(struct c2_net_conn *conn);
	/**
	   Synchronously call operation on the target service and wait for
	   reply.
	 */
	int  (*sio_call)(struct c2_net_conn *conn, struct c2_net_call *c);
	/**
	   Post an asynchronous operation to the target service.

	   The completion is announced by signalling c2_net_call::ac_chan.
	 */
	int  (*sio_send)(struct c2_net_conn *conn, struct c2_net_call *c);
};

/**
   Create a network connection to a given service.

   Allocates resources and connects transport connection to some logical
   connection.  Logical connection is used to send rpc in the context of one or
   more sessions.  (@ref rpc-cli-session)

   @param nid - service identifier

   @retval 0 is OK
   @retval <0 error is hit
*/
int c2_net_conn_create(struct c2_service_id *nid);

/**
   Find a connection to a specified service.

   Scans the list of connections to find a logical connection associated with a
   given nid.

   @param nid service identifier

   @retval NULL if none connections to the node
   @retval !NULL connection info pointer
 */
struct c2_net_conn *c2_net_conn_find(const struct c2_service_id *nid);

/**
   Release a connection.

   Releases a reference on network connection. Reference to transport connection
   is released when the last reference to network connection has been released.
*/
void c2_net_conn_release(struct c2_net_conn *conn);

/**
   Unlink connection from connection list.

   Transport connection(s) are released when the last reference on logical
   connection is released.
 */
void c2_net_conn_unlink(struct c2_net_conn *conn);

/**
   Service call description.
 */
struct c2_net_call {
	/** Connection over which the call is made. */
	struct c2_net_conn     *ac_conn;
	/** Argument. */
	struct c2_fop          *ac_arg;
	/** Result, only meaningful when c2_net_async_call::ac_rc is 0. */
	struct c2_fop          *ac_ret;
	/** Call result for asynchronous call. */
	uint32_t                ac_rc;
	/** Channel where asynchronous call completion is broadcast. */
	struct c2_chan          ac_chan;
	/** Linkage into the queue of pending calls. */
	struct c2_queue_link    ac_linkage;
	/** piggy caiiried addb record. */
	struct c2_addb_rec_header *ac_addb_rec;
};

/**
   Synchronous network call. Caller is blocked until reply message is received.

   @param conn - network connection associated with replier
   @param call - description of the call.
 */
int c2_net_cli_call(struct c2_net_conn *conn, struct c2_net_call *call);

/**
   Asynchronous rpc call. Caller continues without waiting for an answer.

   @param conn - network connection associated with replier
   @param call - asynchronous call description

   @retval 0 OK
   @retval <0 ERROR
*/
int c2_net_cli_send(struct c2_net_conn *conn, struct c2_net_call *call);


/**
 initialize network service and setup incoming messages handler

 This function creates a number of service threads, installs request handlers,
 record the thread infomation for all services.

 @param service data structure to contain all service info
 @param sid service identifier

 @return 0 succees, other value indicates error.
 @see c2_net_service_stop
 */
int c2_service_start(struct c2_service *service,
		     struct c2_service_id *sid);
/**
   Stop network service and release resources associated with it.

   @see c2_net_service_start
 */
void c2_service_stop(struct c2_service *service);

void c2_net_reply_post(struct c2_service *service, struct c2_fop *fop,
		       void *cookie);
/**
 constructor for the network library
 */
int c2_net_init(void);

/**
 destructor for the network library.
 release all allocated resources
 */
void c2_net_fini(void);


extern struct c2_net_xprt c2_net_usunrpc_xprt;
extern struct c2_net_xprt c2_net_ksunrpc_xprt;

/** @} end of net group */

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
