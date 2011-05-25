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
#include "lib/cond.h"
#include "lib/mutex.h"
#include "lib/time.h"
#include "lib/thread.h"
#include "lib/vec.h"
#include "addb/addb.h"

#ifdef __KERNEL__
#include "net/linux_kernel/net_otw_types_k.h"
#else
#include "net/net_otw_types_u.h"
#endif

/**
   @defgroup net Networking

   @brief The networking module provides an asynchronous, event-based message
   passing service, with support for asynchronous bulk data transfer (if used
   with an RDMA capable transport).

   Major data-types in C2 networking are:
   @li Network buffer (c2_net_buffer);
   @li Network buffer descriptor (c2_net_buf_desc);
   @li Network domain (c2_net_domain);
   @li Network end point (c2_net_end_point);
   @li Network event (c2_net_event);
   @li Network transfer machine (c2_net_transfer_mc);
   @li Network transport (c2_net_xprt);

   See <a href="https://docs.google.com/a/xyratex.com/document/d/1tm_IfkSsW6zfOxQlPMHeZ5gjF1Xd0FAUHeGOaNpUcHA/edit?hl=en#">RPC Bulk Transfer Task Plan</a>
   for details on the usage.

   See \ref netDep for older interfaces.

   @{

 */

/*import */
struct c2_fop; /* deprecated */

/* export */
struct c2_net_xprt;
struct c2_net_xprt_ops;
struct c2_net_domain;
struct c2_net_transfer_mc;
struct c2_net_end_point;
struct c2_net_buffer;
struct c2_net_buf_desc;
struct c2_net_event;
struct c2_net_tm_callbacks;
struct c2_net_qstats;

struct c2_service_id;     /* deprecated */
struct c2_service_id_ops; /* deprecated */
struct c2_net_conn;       /* deprecated */
struct c2_net_conn_ops;   /* deprecated */
struct c2_service;        /* deprecated */
struct c2_service_ops;    /* deprecated */
struct c2_net_call;       /* deprecated */

/**
 constructor for the network library
 */
int c2_net_init(void);

/**
 destructor for the network library.
 release all allocated resources
 */
void c2_net_fini(void);

/** Network transport (e.g., lnet or sunrpc) */
struct c2_net_xprt {
	const char                   *nx_name;
	const struct c2_net_xprt_ops *nx_ops;
};

/**
   Network transport operations. The network domain mutex must be
   held to invoke these methods, unless explicitly stated otherwise.
 */
struct c2_net_xprt_ops {
	/**
	   Initialise transport specific part of a domain (e.g., start threads,
	   initialise portals).
	   Only the c2_net_mutex is held across this call.
	 */
	int  (*xo_dom_init)(struct c2_net_xprt *xprt,
			    struct c2_net_domain *dom);
	/**
	   Finalise transport resources in a domain.
	   Only the c2_net_mutex is held across this call.
	 */
	void (*xo_dom_fini)(struct c2_net_domain *dom);

	/**
	   Perform transport level initialization of the transfer machine.
	   @param tm   Transfer machine pointer.
             All fields will be initialized at this time, specifically:
             @li ntm_dom
	     @li ntm_xprt_private - Initialized to NULL. The method can
	     set its own value in the structure.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_tm_init()
	 */
	int (*xo_tm_init)(struct c2_net_transfer_mc *tm);

	/**
	   Initiate the startup of the (initialized) transfer machine.
	   A completion event should be posted when started, using a different
	   thread.
	   <b>Serialized using the transfer machine mutex.</b>
	   @param tm   Transfer machine pointer.
             The following fields are of special interest to this method:
             @li ntm_dom
	     @li ntm_ep - End point associated with the transfer machine.
	     @li ntm_xprt_private
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_tm_start()
	 */
	int (*xo_tm_start)(struct c2_net_transfer_mc *tm);

	/**
	   Initiate the shutdown of a transfer machine, cancelling any
	   pending startup.
	   No incoming messages should be accepted.  Pending operations should
	   drain or be cancelled if requested.
	   A completion event should be posted when stopped, using a different
	   thread.
	   <b>Serialized using the transfer machine mutex.</b>
	   @param tm   Transfer machine pointer.
	   @param cancel Pending outbound operations should be cancelled
	   immediately.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_tm_stop()
	 */
	int (*xo_tm_stop)(struct c2_net_transfer_mc *tm, bool cancel);

	/**
	   Release resources associated with a transfer machine.
	   The transfer machine will be in the stopped state.
	   @param tm   Transfer machine pointer.
             The following fields are of special interest to this method:
             @li ntm_dom
	     @li ntm_xprt_private - The method should free any
	     allocated memory tracked by this pointer.
	   @see c2_net_tm_fini()
	 */
	void (*xo_tm_fini)(struct c2_net_transfer_mc *tm);

	/**
	   Create an end point with a specific address.
	   @param epp     Returned end point data structure.
	   @param dom     Specify the domain pointer.
	   @param addr    Address string.  Could be NULL to
	                  indicate dynamic addressing.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_end_point_create()
	 */
	int (*xo_end_point_create)(struct c2_net_end_point **epp,
				   struct c2_net_domain *dom,
				   const char *addr);

	/**
	   Register the buffer for use with a transfer machine in
	   the manner indicated by the c2_net_buffer.nb_qtype value.
	   @param nb  Buffer pointer with c2_net_buffer.nb_dom set.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_buffer_register()
	 */
	int (*xo_buf_register)(struct c2_net_buffer *nb);

	/**
	   Deregister the buffer from the transfer machine.
	   @param nb  Buffer pointer with c2_net_buffer.nb_tm set.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_buffer_deregister()
	 */
	int (*xo_buf_deregister)(struct c2_net_buffer *nb);

	/**
	   Initiate an operation on a buffer on the transfer machine's
	   queues.

	   In the case of buffers added to the C2_NET_QT_ACTIVE_BULK_RECV
	   or C2_NET_QT_ACTIVE_BULK_SEND queues, the method should validate
	   that the buffer size or data length meet the size requirements
	   encoded within the network buffer descriptor.

	   In the case of the buffers added to the C2_NET_QT_PASSIVE_BULK_RECV
	   or C2_NET_QT_PASSIVE_BULK_SEND queues, the method should set
	   the network buffer descriptor in the specified buffer.

	   The C2_NET_BUF_IN_USE flag will be cleared before invoking the
	   method.   This allows the transport to use this flag to defer
	   operations until later, which is useful if buffers are added
	   during transfer machine state transitions.

	   The C2_NET_BUF_QUEUED flag and the nb_add_time field
	   will be set prior to calling the method.

	   <b>Serialized using the transfer machine mutex.</b>
	   @param nb  Buffer pointer with c2_net_buffer.nb_tm set.
	   For other flags, see struct c2_net_buffer.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_buffer_add(), struct c2_net_buffer
	 */
	int (*xo_buf_add)(struct c2_net_buffer *nb);

	/**
	   Cancel an operation involving a buffer.
	   The method should cancel the operation involving use of the
	   buffer, as described by the value of the c2_net_buffer.nb_qtype
	   field.
	   The C2_NET_BUF_CANCELLED flag should be set in buffers whose
	   operations get cancelled, so c2_net_tm_event_post() can
	   force the right error status.
	   <b>Serialized using the transfer machine mutex.</b>
	   @param nb  Buffer pointer with c2_net_buffer.nb_tm and
	   c2_net_buffer.nb_qtype set.
	   @see c2_net_buffer_del()
	 */
	void (*xo_buf_del)(struct c2_net_buffer *nb);

	/**
	   Retrieve the maximum buffer size (includes all segments).
	   @param dom     Domain pointer.
	   @retval size    Returns the maximum buffer size.
	   @see c2_net_domain_get_max_buffer_size()
	 */
	c2_bcount_t (*xo_get_max_buffer_size)(const struct c2_net_domain *dom);

	/**
	   Retrieve the maximum buffer segment size.
	   @param dom     Domain pointer.
	   @retval size    Returns the maximum segment size.
	   @see c2_net_domain_get_max_buffer_segment_size()
	 */
	c2_bcount_t (*xo_get_max_buffer_segment_size)(const struct c2_net_domain
						      *dom);

	/**
	   Retrieve the maximum number of buffer segments.
	   @param dom      Domain pointer.
	   @retval num_segs Returns the maximum segment size.
	   @see c2_net_domain_get_max_buffer_segment_size()
	 */
	int32_t (*xo_get_max_buffer_segments)(const struct c2_net_domain *dom);

	/**
	   <b>Deprecated.</b>
	   Initialise transport specific part of a service identifier.
	 */
	int  (*xo_service_id_init)(struct c2_service_id *sid, va_list varargs);

	/**
	   <b>Deprecated.</b>
	   Initialise the server side part of a transport.
	 */
	int  (*xo_service_init)(struct c2_service *service);

	/**
	   <b>Deprecated.</b>
	   Interface to return maxima for bulk I/O for network transport e.g.,
	   lnet or sunrpc.

	   The interface can be made generic enough to return any other property
	   of network transport.
	 */
	size_t (*xo_net_bulk_size)(void);
};

/**
 Initialize the transport software.
 A network domain must be initialized to use the transport.
 @param xprt Tranport pointer.
 @retval 0 (success)
 @retval -errno (failure)
 */
int  c2_net_xprt_init(struct c2_net_xprt *xprt);

/**
 Shutdown the transport software.
 All associated network domains should be cleaned up at this point.
 @pre Network domains should have been finalized.
 @param xprt Tranport pointer.
 */
void c2_net_xprt_fini(struct c2_net_xprt *xprt);

/** @}
   @defgroup netDep Networking (Deprecated Interfaces)
   @{
 */
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
         timebase for rate calculations.
	 */
        c2_time_t        ns_time;
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

/** @}
 @addtogroup net Networking.
 @{
 */

/**
   A collection of network resources.
 */
struct c2_net_domain {
	/**
	   This mutex is used to protect the resources associated with
	   a network domain.
	 */
	struct c2_mutex     nd_mutex;

	/**
	   List of c2_net_end_point structures. Managed by the transport.
	 */
	struct c2_list      nd_end_points;

	/**
	   List of c2_net_buffer structures registered with the domain.
	 */
	struct c2_list      nd_registered_bufs;

	/**
	   List of c2_net_transfer_mc structures.
	 */
	struct c2_list      nd_tms;

	/** <b>Deprecated.</b> Network read-write lock */
	struct c2_rwlock    nd_lock;

	/** <b>Deprecated.</b> List of connections in this domain. */
	struct c2_list      nd_conn;
	/** <b>Deprecated.</b> List of services running in this domain. */
	struct c2_list      nd_service;

	/** Transport private domain data. */
	void               *nd_xprt_private;

	/** Pointer to transport */
	struct c2_net_xprt *nd_xprt;

        /** <b>Deprecated.</b> Domain network stats */
        struct c2_net_stats nd_stats[NS_STATS_NR];
	/**
	   ADDB context for events related to this domain
	 */
	struct c2_addb_ctx  nd_addb;
};

/**
   Initialize a domain.
 @param dom Domain pointer.
 @param xprt Tranport pointer.
 @pre dom->nd_xprt == NULL
 @retval 0 (success)
 @retval -errno (failure)
 */
int c2_net_domain_init(struct c2_net_domain *dom, struct c2_net_xprt *xprt);

/**
   Release resources related to a domain.
   @pre All end points, registered buffers and transfer machines released.
   @param dom Domain pointer.
 */
void c2_net_domain_fini(struct c2_net_domain *dom);

/**
   This subroutine is used to determine the maximum buffer size.
   This includes all segments.
   @param dom     Pointer to the domain.
   @retval size    Returns the maximum buffer size.
 */
c2_bcount_t c2_net_domain_get_max_buffer_size(struct c2_net_domain *dom);

/**
   This subroutine is used to determine the maximum buffer segment size.
   @param dom     Pointer to the domain.
   @retval size    Returns the maximum buffer size.
 */
c2_bcount_t c2_net_domain_get_max_buffer_segment_size(struct c2_net_domain
						      *dom);

/**
   This subroutine is used to determine the maximum number of
   buffer segments.
   @param dom      Pointer to the domain.
   @retval num_segs Returns the number of segments.
 */
int32_t c2_net_domain_get_max_buffer_segments(struct c2_net_domain *dom);

/**
   This represents an addressable network end point. Memory for this data
   structure is managed by the network transport component.

   Multiple entities may reference and use the data structure at the same time,
   so a reference count is maintained within it to determine when it is safe to
   release the structure.

   Transports should embed this data structure in their private end point
   structures, and provide the release() method required to free them.
   The release() method should grab the network domain mutex and dequeue the
   data structure from the domain.
 */
struct c2_net_end_point {
	/** Keeps track of usage */
	struct c2_ref          nep_ref;
	/** Pointer to the network domain */
	struct c2_net_domain  *nep_dom;
	/** Linkage in the domain list */
	struct c2_list_link    nep_dom_linkage;
	/** Transport specific printable representation of the
	    end point address.
	 */
	const char            *nep_addr;
};

/**
   Allocates an end point data structure representing the desired
   end point and sets its reference count to 1,
   or increments the reference count of an existing matching data structure.
   The invoker should call the c2_net_end_point_put() when the
   data structure is no longer needed.
   @param epp Pointer to a pointer to the data structure which will be
   set upon return.  The reference count of the returned data structure
   will be at least 1.
   @param dom Network domain pointer.
   @param addr String describing the end point address in a transport specific
   manner.  The format of this address string is the same as the printable
   representation form stored in the end point nep_addr field.  It is optional,
   and if NULL, the transport may support assignment of an end point with a
   dynamic address; however this is not guaranteed.
   The address string, if specified, is not referenced again after return from
   this subroutine.
   @see c2_net_end_point_get(), c2_net_end_point_put()
   @post (*epp)->nep_ref->ref_cnt >= 1 && (*epp)->nep_addr != NULL
   @retval 0 on success
   @retval -errno on failure
 */
int c2_net_end_point_create(struct c2_net_end_point **epp,
			    struct c2_net_domain     *dom,
			    const char               *addr);

/**
   Increment the reference count of an end point data structure.
   This is used to safely point to the structure in a different context -
   when done, the reference count should be decremented by a call to
   c2_net_end_point_put().

   @param ep End point data structure pointer.
   @pre ep->nep_ref->ref_cnt >= 1
 */
void c2_net_end_point_get(struct c2_net_end_point *ep);

/**
   Decrement the reference count of an end point data structure.
   The structure will be released when the count goes to 0.
   @param ep End point data structure pointer.
   Do not dereference this pointer after this call.
   @pre ep->nep_ref->ref_cnt >= 1
   @note The domain lock will be obtained internally to synchronize the
   transport provided release() method in case the end point gets released.
   @retval 0 (success)
   @retval -errno (failure)
 */
int c2_net_end_point_put(struct c2_net_end_point *ep);

/**
    This enumeration describes the types of logical queues in a transfer
    machine.

    <b>Note:</b> We use the term "queue" here to imply that the
    order of addition
    matters; in reality, while it <i>may</i> matter, external factors have
    a <i>much</i> larger influence on the actual order in which buffer
    operations complete; we're not implying FIFO semantics here!
    Consider:
    - The underlying transport manages message buffers.
    Since there is a great deal of concurrency and latency involved with
    network communication, and message sizes can vary,
    a completed (sent or received) message buffer is not necessarily the first
    that was added to its "queue" for that purpose.
    - The upper protocol layers of the <i>remote</i> end points are
    responsible for initiating bulk data transfer operations, which ultimately
    determines when passive buffers complete in <i>this</i> process.
    The remote upper protocol layers can, and probably will, reorder requests
    into an optimal order for themselves, which does not necessarily correspond
    to the order in which the passive bulk data buffers were added.

    The fact of the matter is that the transfer machine itself
    is really only interested in tracking buffer existence and uses
    lists and not queues internally.
 */
enum c2_net_queue_type {
	/**
	   Queue with buffers to receive messages.
	 */
	C2_NET_QT_MSG_RECV=0,

	/**
	   Queue with buffers with messages to send.
	 */
	C2_NET_QT_MSG_SEND,

	/**
	   Queue with buffers awaiting completion of
	   remotly initiated bulk data send operations
	   that will read from these buffers.
	 */
	C2_NET_QT_PASSIVE_BULK_RECV,

	/**
	   Queue with buffers awaiting completion of
	   remotly initiated bulk data receive operations
	   that will write to these buffers.
	 */
	C2_NET_QT_PASSIVE_BULK_SEND,

	/**
	   Queue with buffers awaiting completion of
	   locally initiated bulk data receive operations
	   that will read from passive buffers.
	 */
	C2_NET_QT_ACTIVE_BULK_RECV,

	/**
	   Queue with buffers awaiting completion of
	   locally initiated bulk data send operations
	   to passive buffers.
	 */
	C2_NET_QT_ACTIVE_BULK_SEND,

	C2_NET_QT_NR
};

/**
   A transfer machine exists in one of the following states.
 */
enum c2_net_tm_state {
	C2_NET_TM_UNDEFINED=0, /**< Undefined, prior to initialization */
	C2_NET_TM_INITIALIZED, /**< Initialized */
	C2_NET_TM_STARTING,    /**< Startup in progress */
	C2_NET_TM_STARTED,     /**< Active */
	C2_NET_TM_STOPPING,    /**< Shutdown in progress */
	C2_NET_TM_STOPPED,     /**< Stopped */
	C2_NET_TM_FAILED       /**< Failed TM, must be fini'd */
};

/**
   Event types are defined by this enumeration.
 */
enum c2_net_ev_type {
	C2_NET_EV_ERROR=0,        /**< General error */
	C2_NET_EV_STATE_CHANGE,   /**< Transfer machine state change event */
	C2_NET_EV_BUFFER_RELEASE, /**< Buffer operation completion event */
	C2_NET_EV_DIAGNOSTIC,     /**< Diagnostic event */
	C2_NET_EV_NR
};

/**
   Data structure used to provide asynchronous notification of
   significant events, such as the completion of buffer operations,
   transfer machine state changes and general errors.

   All events have the following fields set:
   - nev_type
   - nev_tm
   - nev_time
   - nev_status

   The nev_type field should be referenced to determine the type of
   event, and which other fields of this structure get set:

   - C2_NET_EV_ERROR provides error notification, out of the context of
     any buffer operation completion, or a transfer machine state change.
     No additional fields are set.
   - C2_NET_EV_STATE_CHANGE provides notification of a transfer machine
     state change.
     The nev_next_state field describes the destination state.
     Refer to the nev_status field to determine if the operation succeeded.
   - C2_NET_EV_BUFFER_RELEASE provides notification of the completion of
     a buffer operation.
     The nev_buffer field points to the buffer.
     Refer to the nev_status field to determine if the operation succeeded.
   - C2_NET_EV_DIAGNOSTIC provides diagnostic information.
     The nev_payload field may point to transport specific data.
     The API does not require nor specify how a transport produces
     diagnostic information, but does require that diagnostic events
     not be produced unless explicitly requested.

   This data structure is typically assigned on the stack of the thread
   that invokes the c2_net_tm_event_post() subroutine.  Applications
   should not attempt to save a reference to it from their callback
   functions.

   @see c2_net_tm_event_post() for details on event delivery concurrency.
 */
struct c2_net_event {
	/**
	   Indicates the type of event.
	   Other fields get set depending on the value of this field.
	 */
	enum c2_net_ev_type        nev_type;

	/**
	   Transfer machine pointer.
	 */
	struct c2_net_transfer_mc *nev_tm;

	/**
	   Time the event is posted.
	 */
	c2_time_t                  nev_time;

	/**
	   Status or error code associated with the event.

	   In all event types other than C2_NET_EV_DIAGNOSTIC, a 0 in this
	   field implies successful completion, and a negative error number
	   is used to indicate the reasons for failure.
	   The following errors are well defined:
	   	- <b>-ENOBUFS</b> This indicates that the transfer machine
		lost messages due to a lack of receive buffers.
	   	- <b>-ECANCELED</b> This is used in buffer release events to
		indicate that the associated buffer operation was
		cancelled by a call to c2_net_buffer_del().

	   Diagnostic events are free to make any use of this field.
	 */
	int32_t                    nev_status;

	/**
	   Valid only if the nev_type is C2_NET_EV_BUFFER_RELEASE.

	   Pointer to the buffer concerned.
	 */
	struct c2_net_buffer      *nev_buffer;

	/**
	   Valid only if the nev_type is C2_NET_EV_STATE_CHANGE.

	   The next state of the transfer machine is set in this field.
	   Any associated error condition defined by the nev_status field.
	 */
	enum c2_net_tm_state       nev_next_state;

	/**
	   Valid only if the nev_type is C2_NET_EV_STATE_DIAGNOSTIC.

	   Transports may use this to point to internal data; they
	   could also choose to embed the event data structure
	   in a transport specific structure appropriate to the event.
	   Either approach would be of use to a diagnostic application.
	 */
	void                      *nev_payload;
};

/**
   A transfer machine is notified of events of interest with this subroutine.
   Typically, the subroutine is invoked by the transport associated with
   the transfer machine.

   The event data structure is not referenced from
   elsewhere after this subroutine returns, so may be allocated on the
   stack of the calling thread.

   The subroutine provides the following concurrency semantics for event
   delivery:
   - Multiple concurrent events may be delivered for a given transfer machine.
   - Only one event may be delivered at a time for a given buffer on a
   transfer machine queue.  The invoking process will block on the transfer
   machine's c2_net_transfer_mc.ntm_cond condition variable until the
   outstanding event callback on the buffer completes (indicated by the
   C2_NET_BUF_IN_CALLBACK bit being cleared).

   If the event type is C2_NET_EV_BUFFER_RELEASE, then the
   subroutine will remove the buffer from its queue, and clear its
   C2_NET_BUF_QUEUED, C2_NET_BUF_IN_USE and C2_NET_BUF_CANCELLED flags
   prior to invoking the callback.  If the C2_NET_BUF_CANCELLED flag was
   set, then the status is forced to -ECANCELED.

   The subroutine will also signal to all waiters on the
   c2_net_transfer_mc.ntm_chan field after delivery of the callback.

   The invoking process should be aware that the callback subroutine could
   end up making re-entrant calls to the transport layer.

   @param ev Event pointer. The nev_tm field identifies the transfer machine.
 */
void c2_net_tm_event_post(const struct c2_net_event *ev);


/**
   Callback function pointer type.
   @param ev Pointer to the event. The event data structure is
   released upon return from the subroutine.
 */
typedef void (*c2_net_tm_cb_proc_t)(const struct c2_net_event *ev);

/**
   This data structure contains application callback function pointers.

   Every struct c2_net_transfer_mc requires a pointer to one such structure.
   Additionally, a struct c2_net_buffer also permits the application to
   specify a pointer to callbacks appropriate to the buffer.

   Multiple objects can point to a single instance of such a structure.

   @see c2_net_tm_event_post() for the concurrency semantics.
 */
struct c2_net_tm_callbacks {
	/**
	   Optional callback for buffers on the C2_NET_QT_MSG_RECV queue.
	   Invoked when a message is received.
	   @pre ev->nev_type == C2_NET_EV_BUFFER_RELEASE &&
	        ev->nev_buffer != NULL &&
		ev->nev_buffer->nb_qtype == C2_NET_QT_MSG_RECV
	 */
	c2_net_tm_cb_proc_t ntc_msg_recv_cb;

	/**
	   Optional callback for buffers on the C2_NET_QT_MSG_SEND queue.
	   Invoked when a message is sent.
	   @pre ev->nev_type == C2_NET_EV_BUFFER_RELEASE &&
	        ev->nev_buffer != NULL &&
		ev->nev_buffer->nb_qtype == C2_NET_QT_MSG_SEND
	 */
	c2_net_tm_cb_proc_t ntc_msg_send_cb;

	/**
	   Optional callback for buffers on the C2_NET_QT_PASSIVE_BULK_RECV
	   queue.
	   Invoked when data has been written to the buffer on the completion
	   of a remotely initiated bulk transfer operation.
	   @pre ev->nev_type == C2_NET_EV_BUFFER_RELEASE &&
	        ev->nev_buffer != NULL &&
		ev->nev_buffer->nb_qtype == C2_NET_QT_PASSIVE_BULK_RECV
	 */
	c2_net_tm_cb_proc_t ntc_passive_bulk_recv_cb;

	/**
	   Optional callback for buffers on the C2_NET_QT_PASSIVE_BULK_SEND
	   queue.
	   Invoked when data has been read from the buffer on the completion
	   of a remotely initiated bulk transfer operation.
	   @pre ev->nev_type == C2_NET_EV_BUFFER_RELEASE &&
	        ev->nev_buffer != NULL &&
		ev->nev_buffer->nb_qtype == C2_NET_QT_PASSIVE_BULK_SEND
	 */
	c2_net_tm_cb_proc_t ntc_passive_bulk_send_cb;

	/**
	   Optional callback for buffers on the C2_NET_QT_ACTIVE_BULK_RECV
	   queue.
	   Invoked when data has been written to the buffer on the completion
	   of a locally initiated bulk transfer operation.
	   @pre ev->nev_type == C2_NET_EV_BUFFER_RELEASE &&
	        ev->nev_buffer != NULL &&
		ev->nev_buffer->nb_qtype == C2_NET_QT_ACTIVE_BULK_RECV
	 */
	c2_net_tm_cb_proc_t ntc_active_bulk_recv_cb;

	/**
	   Optional callback for buffers on the C2_NET_QT_ACTIVE_BULK_SEND
	   queue.
	   Invoked when data has been sent from the buffer on the completion
	   of a locally initiated bulk transfer operation.
	   @pre ev->nev_type == C2_NET_EV_BUFFER_RELEASE &&
	        ev->nev_buffer != NULL &&
		ev->nev_buffer->nb_qtype == C2_NET_QT_ACTIVE_BULK_SEND
	 */
	c2_net_tm_cb_proc_t ntc_active_bulk_send_cb;

	/**
	   Required callback. Invoked in case of error and when
	   an optional buffer specific callback is missing.
	 */
	c2_net_tm_cb_proc_t ntc_event_cb;
};

/**
   Statistical data maintained for each transfer machine queue.
   It is up to the higher level layers to retrieve the data and
   reset the statistical counters.
 */
struct c2_net_qstats {
	/** The number of add operations performed */
	uint64_t        nqs_num_adds;

	/** The number of del operations performed */
	uint64_t        nqs_num_dels;

	/** The number of successful events posted on buffers in the queue */
	uint64_t        nqs_num_s_events;

	/** The number of failure events posted on buffers in the queue.

	    In the case of the C2_NET_QT_MSG_RECV queue, the failure
	    counter is also incremented when the queue is empty when a
	    message arrives.
	 */
	uint64_t        nqs_num_f_events;

	/**
	    The total of time spent in the queue by all buffers
	    measured from when they were added to the queue
	    to the time their completion event got posted.
	 */
	c2_time_t       nqs_time_in_queue;

	/**
	   The total number of bytes processed by buffers in the queue.
	   Computed at completion.
	 */
	uint64_t        nqs_total_bytes;

	/**
	   The maximum number of bytes processed in a single
	   buffer in the queue.
	   Computed at completion.
	 */
	uint64_t        nqs_max_bytes;
};

/**
   This data structure tracks message buffers and supports callbacks to notify
   the application of changes in state associated with these buffers.
 */
struct c2_net_transfer_mc {
	/** Pointer to application callbacks. Should be set before
	    initialization.
	 */
	const struct c2_net_tm_callbacks *ntm_callbacks;

	/** Specifies the transfer machine state */
	enum c2_net_tm_state        ntm_state;

	/**
	   Mutex associated with the transfer machine.
	   The mutex is used when the transfer machine state is in
	   the bounds:
	   C2_NET_TM_INITIALIZED < state < C2_NET_TM_STOPPED.

	   Most transfer machine operations are protected by this
	   mutex.  The presence of this mutex in the transfer machine
	   provides a tighter locus of memory accesses to the data
	   structures associated with the operation of a single transfer
	   machine, than would occur were the domain mutex used.
	   It also reduces the memory access overlaps between individual
	   transfer machines.  Transports could use this memory
	   access pattern to provide processor-affinity support for
	   buffer operation on a per-transfer-machine-per-processor basis,
	   by invoking the buffer operation callbacks on the same
	   processor used to submit the buffer operation.

	   It is not permitted to obtain this mutex when holding the
	   domain mutex. The inverse locking order is permitted.
	 */
	struct c2_mutex             ntm_mutex;

	/**
	   Condition variable associated with the transfer machine.
	   Use it with the network domain's mutex.
	 */
	struct c2_cond              ntm_cond;

	/**
	   Callback activity is tracked by this counter.
	   It is incremented by c2_net_tm_post_event() before invoking
	   a callback, and decremented when it returns.
	 */
	uint32_t                    ntm_callback_counter;

	/** Network domain pointer */
	struct c2_net_domain       *ntm_dom;

	/**
	   End point associated with this transfer machine.
	   Messages sent from this
	   transfer machine appear to have originated from this end point.
	   It is provided by the application during the
	   call to c2_net_tm_start().
	   @note The assumption here is that the transport can maintain
	   separate receive message buffer pools for each transfer machine.
	 */
        struct c2_net_end_point    *ntm_ep;

	/**
	   Waiters for event notifications. They do not get copies
	   of the event.
	 */
	struct c2_chan              ntm_chan;

	/**
	   Lists of c2_net_buffer structures by queue type.
	 */
	struct c2_list              ntm_q[C2_NET_QT_NR];

	/** Statistics maintained per logical queue */
	struct c2_net_qstats        ntm_qstats[C2_NET_QT_NR];

	/** Domain linkage */
	struct c2_list_link         ntm_dom_linkage;

	/** Transport private data */
        void                       *ntm_xprt_private;
};

/**
   Initialize a transfer machine.
   @param tm  Pointer to transfer machine data structure to be initialized.

   Prior to invocation the following fields should be set:
   - c2_net_transfer_mc.ntm_state should be set to C2_NET_TM_UNDEFINED.
   Zeroing the entire structure has the side effect of setting this value.
   - c2_net_transfer_mc.ntm_callbacks should point to a properly initialized
   struct c2_net_tm_callbacks data structure.

   All fields in the structure other then the above will be set to their
   appropriate initial values.
   @note An initialized TM cannot be fini'd without first starting it.
   @param dom Network domain pointer.
   @retval 0 (success)
   @retval -errno (failure)
 */
int c2_net_tm_init(struct c2_net_transfer_mc *tm, struct c2_net_domain *dom);

/**
   Finalize a transfer machine, releasing any associated
   transport specific resources.
   @pre tm->ntm_state == C2_NET_TM_STOPPED ||
        tm->ntm_state == C2_NET_TM_FAILED ||
	tm->ntm_state == C2_NET_TM_INITIALIZED
   @param tm Transfer machine pointer.
 */
void c2_net_tm_fini(struct c2_net_transfer_mc *tm);

/**
   Start a transfer machine.

   The subroutine does not block the invoker. Instead the state is
   immediately changed to C2_NET_TM_STARTING, and an event will be
   posted to indicate when the transfer machine has transitioned to
   the C2_NET_TM_STARTED state.

   @note It is possible that the state change event be posted before this
   subroutine returns.
   It is guaranteed that the event will be posted on a different thread.

   @pre tm->ntm_state == C2_NET_TM_INITIALIZED
   @param tm  Transfer machine pointer.
   @param ep  End point to associate with the transfer machine.
   @retval 0 (success)
   @retval -errno (failure)
 */
int c2_net_tm_start(struct c2_net_transfer_mc *tm,
		    struct c2_net_end_point *ep);

/**
   Initiate the shutdown of a transfer machine.  New messages will
   not be accepted.  Pending operations will be completed or
   aborted as desired.

   The subroutine does not block the invoker.  Instead the state is
   immediately changed to C2_NET_TM_STOPPING, and an event will be
   posted to indicate when the transfer machine has transitioned to
   the C2_NET_TM_STOPPED state.

   @note It is possible that the state change event be posted before this
   subroutine returns.
   It is guaranteed that the event will be posted on a different thread.

   @pre
(tm->ntm_state == C2_NET_TM_INITIALIZED) ||
(tm->ntm_state == C2_NET_TM_STARTING) ||
(tm->ntm_state == C2_NET_TM_STARTED)
   @param tm  Transfer machine pointer.
   @param abort Cancel pending operations.
   Support for the implementation of this option is transport specific.
   @retval 0 (success)
   @retval -errno (failure)
 */
int c2_net_tm_stop(struct c2_net_transfer_mc *tm, bool abort);

/**
   Retrieve transfer machine statistics for all or for a single logical queue,
   optionally resetting the data.  The operation is performed atomically
   with respect to on-going transfer machine activity.
   @pre tm->ntm_state >= C2_NET_TM_INITIALIZED
   @param tm     Transfer machine pointer
   @param qtype  Logical queue identifier of the queue concerned.
   Specify C2_NET_QT_NR instead if all the queues are to be considered.
   @param qs     Returned statistical data. May be NULL if only a reset
   operation is desired.  Otherwise should point to a single c2_net_qstats
   data structure if the value of <b>qtype</b> is not C2_NET_QT_NR, or
   else should point to an array of C2_NET_QT_NR such structures in which to
   return the statistical data on all the queues.
   @param reset  Specify <b>true</b> if the associated statistics data
   should be reset at the same time. Otherwise specify <b>false</b>.
   @retval 0 (success)
   @retval -errno (failure)
 */
int c2_net_tm_stats_get(struct c2_net_transfer_mc *tm,
			enum c2_net_queue_type qtype,
			struct c2_net_qstats *qs,
			bool reset);

/**
   Buffer state is tracked using these bitmap flags.
 */
enum c2_net_buf_flags {
	/** Set when the buffer is registered with the domain */
	C2_NET_BUF_REGISTERED  = 1<<0,
	/** Set when the buffer is added to a transfer machine logical queue */
	C2_NET_BUF_QUEUED      = 1<<1,
	/** Set when the transport starts using the buffer */
	C2_NET_BUF_IN_USE      = 1<<2,
	/** Set for the duration of a callback on the buffer */
	C2_NET_BUF_IN_CALLBACK = 1<<3,
	/** Indicates that the buffer operation has been cancelled */
	C2_NET_BUF_CANCELLED   = 1<<4,
};

/**
   This data structure is used to track the memory used
   for message passing or bulk data transfer over the network.

   Support for scatter-gather buffers is provided by use of a c2_bufvec;
   upper layer protocols may impose limitations on the
   use of scatter-gather, especially for message passing.
   The transport will impose limitations on the number of vector elements
   and the overall maximum buffer size.

   The invoking application is responsible for the creation
   of these data structures, registering them with the network
   domain, and providing them to a transfer machine for specific
   operations.
   As such, memory alignment considerations of the encapsulated
   c2_bufvec are handled by the invoking application.

   Once the application initiates an operation on a buffer, it should
   refrain from modifying the buffer until the callback signifying
   operation completion.

   Applications must register buffers with the transport before use,
   and deregister them before shutting down.
 */
struct c2_net_buffer {
	/**
	   Vector pointing to memory associated with this data structure.
	   Initialized by the application prior to registration.
	   It should not be modified until after registration.
	 */
	struct c2_bufvec           nb_buffer;

	/**
	   The actual amount of data received or to be transferred.
	   Its usage varies by context:
	   - The application should set the value before adding a buffer
	   to the C2_NET_QT_MSG_SEND, C2_NET_QT_PASSIVE_BULK_SEND or
	   C2_NET_QT_ACTIVE_BULK_SEND queues.
	   - This value is set by the transport at the time the
	   completion event is posted for a buffer on the
	   C2_NET_QT_MSG_RECV, C2_NET_QT_PASSIVE_BULK_RECV or
	   C2_NET_QT_ACTIVE_BULK_RECV queues.
	 */
	c2_bcount_t                nb_length;

	/**
	   Domain pointer. It is set automatically when the buffer
	   is registered with c2_net_buffer_register().
	   The application should not modify this field.
	 */
	struct c2_net_domain      *nb_dom;

	/**
	   Transfer machine pointer. It is set automatically with
	   every call to c2_net_buffer_add().
	 */
	struct c2_net_transfer_mc *nb_tm;

	/**
	   The application should set this value to identify the logical
	   transfer machine queue before calling c2_net_buffer_add().
	 */
        enum c2_net_queue_type     nb_qtype;

	/**
	    Pointer to application callbacks. Should be set
	    before adding the buffer to a transfer machine logical queue.

	    This is an optional field - set it to NULL if no buffer
	    specific callbacks are desired. Instead, the callbacks from
	    the associated transfer machine will be invoked.

	    If specified, these callbacks will be used for buffer completion
	    events instead of the callbacks specified in the transfer machine.
	    This helps improve the modularity of the upper protocol layers.
	 */
	const struct c2_net_tm_callbacks *nb_callbacks;

	/**
	   Absolute time by which an operation involving the buffer should
	   stop with failure if not completed.
	   The application should set this field prior to adding the
	   buffer to a transfer machine logical queue.

	   <b>Support for this is transport specific.</b>
	   Set the value to C2_TIME_NEVER to disable the timeout.
	 */
	c2_time_t                  nb_timeout;

	/**
	   Time at which the buffer was added to its logical queue.
	   Set by the c2_net_buffer_add() subroutine and used to
	   compute the time spent in the queue.
	 */
	c2_time_t                  nb_add_time;

	/**
	   Network transport descriptor.

	   The value is set upon return from c2_net_buffer_add()
	   when the buffer is added to
	   the C2_NET_QT_PASSIVE_BULK_RECV or C2_NET_QT_PASSIVE_BULK_SEND
	   queues.

	   Applications should convey the descriptor to the active side
	   to perform the bulk data transfer. The active side application
	   code should set this value when adding the buffer to
	   the C2_NET_QT_ACTIVE_BULK_RECV or C2_NET_QT_ACTIVE_BULK_SEND
	   queues, using the c2_net_desc_copy() subroutine.

	   In both cases, applications are responsible for freeing the
	   memory used by this descriptor with the c2_net_desc_free()
	   subroutine.
	 */
	struct c2_net_buf_desc     nb_desc;

	/**
	   This field identifies an end point. Its usage varies by context:

	   - In received messages (C2_NET_QT_MSG_RECV queue) the transport
	   will set the end point to identify the sender of the message
	   before invoking the completion callback on the buffer.
	   The end point will be released when the callback returns.
	   - When sending messages
	   the application should specify the end point of the destination
	   before adding the buffer to the C2_NET_QT_MSG_SEND queue.
	   - When adding a buffer to the C2_NET_QT_PASSIVE_BULK_RECV or
	   C2_NET_PASSIVE_BULK_SEND queues, the application must set this
	   field to identify the end point that will initiate the bulk data
	   transfer.

	   The field is not used for the active bulk cases.
	 */
	struct c2_net_end_point   *nb_ep;

	/**
	   Linkage into one of the transfer machine lists that implement the
	   logical queues.
	   There is only one linkage for all of the queues, as a buffer
	   can only be used for one type of operation at a time.

	   The application should not modify this field.
	 */
	struct c2_list_link        nb_tm_linkage;

	/**
	   Linkage into one of the domain list that tracks registered buffers.

	   The application should not modify this field.
	 */
	struct c2_list_link        nb_dom_linkage;

	/**
	   Transport private data associated with the buffer.
	   Will be freed when the buffer is deregistered, if not earlier.

	   The application should not modify this field.
	 */
        void                      *nb_xprt_private;

	/**
	   Buffer state is tracked with bitmap flags from
	   enum c2_net_buf_flags.

	   The application should initialize this field to 0 prior
	   to registering the buffer with the domain.

	   The application should not modify these flags again until
	   after de-registration.
	 */
	uint64_t                   nb_flags;

	/**
	   The status value posted in the last completion event on
	   the buffer.  The value is placed there just prior to invoking
	   the completion callback on the buffer for application
	   reference after the event callback completes.

	   A value of -ECANCELED is set if the buffer operation was
	   cancelled with c2_net_buffer_del().
	 */
	int32_t                    nb_status;
};

/**
   Register a buffer with the domain. The domain could perform some
   optimizations under the covers.
   @pre
(buf->nb_flags == 0) &&
(buf->nb_buffer.ov_buf != NULL) &&
c2_vec_count(&buf->nb_buffer.ov_vec) > 0
   @post ergo(result == 0, buf->nb_flags & C2_NET_BUF_REGISTERED)
   @param buf Pointer to a buffer. The buffer should have the following fields
   initialized:
   - c2_net_buffer.nb_buffer should be initialized to point to the buffer
   memory regions.
   @param dom Pointer to the domain.
   @retval 0 (success)
   @retval -errno (failure)
 */
int c2_net_buffer_register(struct c2_net_buffer *buf,
			   struct c2_net_domain *dom);

/**
   Deregister a previously registered buffer and releases any transport
   specific resources associated with it.
   The buffer should not be in use, nor should this subroutine be
   invoked from a callback.
   @pre
(buf->nb_flags == C2_NET_BUF_REGISTERED) &&
(buf->nb_dom == dom)
   @param buf Specify the buffer pointer.
   @param dom Specify the domain pointer.
   @retval 0 (success)
   @retval -errno (failure)
 */
int c2_net_buffer_deregister(struct c2_net_buffer *buf,
			     struct c2_net_domain *dom);

/**
   Add a registered buffer to a transfer machine's logical queue specified
   by the c2_net_buffer.nb_qtype value.
   - Buffers added to the C2_NET_QT_MSG_RECV queue are used to receive
   messages.
   - When data is contained in the buffer, as in the case of the
   C2_NET_QT_MSG_SEND, C2_NET_PASSIVE_BULK_SEND and C2_NET_ACTIVE_BULK_SEND
   queues, the application must set the c2_net_buffer.nb_length field to the
   actual length of the data to be transferred.
   - Buffers added to the C2_NET_QT_MSG_SEND queue must identify the
   message destination end point with the c2_net_buffer.nb_ep field.
   - Buffers added to the C2_NET_QT_PASSIVE_BULK_RECV or
   C2_NET_PASSIVE_BULK_SEND queues must have their c2_net_buffer.nb_ep
   field set to identify the end point that will initiate the bulk data
   transfer.  Upon return from this subroutine the c2_net_buffer.nb_desc
   field will be set to the network buffer descriptor to be conveyed to
   said end point.
   - Buffers added to the C2_NET_QT_ACTIVE_BULK_RECV or
   C2_NET_ACTIVE_BULK_SEND queues must have their c2_net_buffer.nb_desc
   field set to the network buffer descriptor associated with the passive
   buffer.

   The buffer should not be modified until the operation completion
   callback is invoked for the buffer.

   The buffer completion callback is guaranteed to be invoked on a
   different thread.

   @pre
(buf->nb_dom == tm->ntm_dom) &&
(tm->ntm_state == C2_NET_TM_STARTED) &&
c2_net__qtype_is_valid(buf->nb_qtype) &&
(buf->nb_flags & C2_NET_BUF_REGISTERED) &&
((buf->nb_flags & C2_NET_BUF_QUEUED) == 0) &&
(buf->nb_qtype != C2_NET_QT_MSG_RECV || buf->nb_ep == NULL)
   @param buf Specify the buffer pointer.
   @param tm  Specify the transfer machine pointer
   @retval 0 (success)
   @retval -errno (failure)
 */
int c2_net_buffer_add(struct c2_net_buffer *buf,
		      struct c2_net_transfer_mc *tm);

/**
   Remove a registered buffer from a logical queue, if possible,
   cancelling any operation in progress.

   <b>Cancellation support is provided by the underlying transport.</b> It is
   not guaranteed that actual cancellation of the operation in progress will
   always be supported, and even if it is, there are race conditions in the
   execution of this request and the concurrent invocation of the completion
   callback on the buffer.

   The transport should set the C2_NET_BUF_CANCELLED flag in the buffer if
   the operation has not yet started.  The flag will be cleared by
   c2_net_tm_event_post().

   The buffer completion callback is guaranteed to be invoked on a
   different thread.

   @pre (buf->nb_flags & C2_NET_BUF_REGISTERED)
   @param buf Specify the buffer pointer.
   @param tm  Specify the transfer machine pointer.
 */
void c2_net_buffer_del(struct c2_net_buffer *buf,
		       struct c2_net_transfer_mc *tm);

/**
   Copy a network buffer descriptor.
   @param from_desc Specifies the source descriptor data structure.
   @param to_desc Specifies the destination descriptor data structure.
   @retval 0 (success)
   @retval -errno (failure)
 */
int c2_net_desc_copy(const struct c2_net_buf_desc *from_desc,
		     struct c2_net_buf_desc *to_desc);

/**
   Free a network buffer descriptor.
   @param desc Specify the network buffer descriptor. Its fields will be
   cleared after this operation.
 */
void c2_net_desc_free(struct c2_net_buf_desc *desc);

/** @} end of networking group


   @addtogroup netDep Networking (Deprecated Interfaces)
   @{
 */
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
 @retval rate, in percent * 100 of maximum seen rate (e.g. 1234 = 12.34%)
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


extern struct c2_net_xprt c2_net_usunrpc_xprt;
extern struct c2_net_xprt c2_net_ksunrpc_xprt;

/** @} end of deprecated net group */

#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
