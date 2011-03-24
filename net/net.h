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
#include "lib/vec.h"
#include "addb/addb.h"

#ifdef __KERNEL__
#include "net/net_otw_types_k.h"
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
	   Perform transport level initialization of the transfer machine.
	   @param tm   Transfer machine pointer. 
             The following fields will be initialized at this time:
             @li ntm_dom
	     @li ntm_xprt_private - Initialized to NULL. The method can
	     set its own value in the structure.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_tm_init()
	 */
	int (*xo_tm_init)(struct c2_net_transfer_mc *tm);

	/**
	   Start the (initialized) transfer machine.
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
	   Stop an initialized transfer machine (may or may not have
	   been started  yet).
	   @param tm   Transfer machine pointer. 
             The following fields are of special interest to this method:
             @li ntm_dom
	     @li ntm_xprt_private - The method should free any
	     allocated memory tracked by this pointer.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_tm_fini()
	 */
	int (*xo_tm_fini)(struct c2_net_transfer_mc *tm);

	/**
	   Create an end point with a specific address based on the
	   variable arguments, or else dynamically assign one.
	   @param tm      Transfer machine pointer. 
	   @param epp     Returned end point data structure.
	   @param varargs Variable arguments. Could be empty.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_end_point_create()
	 */
	int (*xo_end_point_create)(struct c2_net_transfer_mc *tm,
				   struct c2_net_end_point **epp,
				   va_list varargs);

	/**
	   Release the end point. Invoked when its reference count 
	   goes to zero.
	   @param ep   End point pointer.
	   @see c2_net_end_point_put()
	 */
	void (*xo_end_point_release)(struct c2_net_end_point *ep);

	/**
	   Register the buffer for use with a transfer machine in
	   the manner indicated by the c2_net_buffer.nb_qtype value.
	   @param buf  Buffer pointer with c2_net_buffer.nb_tm set.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_buffer_register()
	 */
	int (*xo_buf_register)(struct c2_net_buffer *nb);

	/**
	   Deregister the buffer from the transfer machine.
	   @param buf  Buffer pointer with c2_net_buffer.nb_tm set.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_buffer_deregister()
	 */
	int (*xo_buf_deregister)(struct c2_net_buffer *nb);

	/**
	   Add the buffer to the transfer machine queue identified by 
	   the c2_net_buffer.nb_qtype value.
	   @param buf  Buffer pointer with c2_net_buffer.nb_tm set.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_buffer_add()
	 */
	int (*xo_buf_add)(struct c2_net_buffer *nb);

	/**
	   Remove the buffer from the transfer machine queue identified by the 
	   c2_net_buffer.nb_qtype value.
	   @param buf  Buffer pointer with c2_net_buffer.nb_tm set.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_buffer_del()
	 */
	int (*xo_buf_del)(struct c2_net_buffer *nb);

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

/** @}
 @addtogroup net Networking.
 @{
*/

/**
   A collection of network resources.
 */
struct c2_net_domain {
	struct c2_rwlock    nd_lock;

	/** List of c2_net_transfer_mc structures */
	struct c2_list      nd_tms;

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
 @retval 0 (success)
 @retval -errno (failure)
 */
int c2_net_domain_init(struct c2_net_domain *dom, struct c2_net_xprt *xprt);

/**
   Release resources related to a domain.
 @param dom Domain pointer.
 */
void c2_net_domain_fini(struct c2_net_domain *dom);

/**
   This represents an addressable network end point. Memory for this data
   structure is managed by the network transport component.
   
   Multiple entities may reference and use the data structure at the same time, 
   so a reference count is maintained within it to determine when it is safe to
   release the structure.

   Transports should embed this data structure in their private end point
   structures.
 */
struct c2_net_end_point {
	/** Keeps track of usage */
	struct c2_ref          nep_ref;
	/** Pointer to the transfer machine */
	struct c2_transfer_mc *nep_tm;
	/** Linkage in the transfer machine list */
	struct c2_list_link    nep_linkage;
};

/**
   Allocates an end point data structure representing the desired
   end point and sets its reference count to 1, 
   or increments the reference count of an existing matching data structure.  
   The invoker should call the c2_net_end_point_put() when the
   data structure is no longer needed.
   @param tm  Transfer machine pointer.
   @param epp Pointer to a pointer to the data structure which will be
   set upon return.  The reference count of the returned data structure
   will be at least 1.
   @param ... Transport specific variable arguments describing the 
   end point address. These are optional, and if missing, the transport
   will assign an end point with a dynamic, new address.
   @see c2_net_end_point_get(), c2_net_end_point_put()
 */
int c2_net_end_point_create(struct c2_net_transfer_mc  *tm,
			    struct c2_net_end_point   **epp,
			    ...);

/**
   Increment the reference count of an end point data structure.
   This is used to safely point to the structure in a different context -
   when done, the reference count should be decremented by a call to
   c2_net_end_point_put().

   @param ep End point data structure pointer.
   The subroutine will assert that the existing reference count is
   at least 1.
   @retval 0 (success)
   @retval -errno (failure)
*/
int c2_net_end_point_get(struct c2_net_end_point *ep);

/**
   Decrement the reference count of an end point data structure.
   The structure will be released when the count goes to 0.
   @param ep End point data structure pointer. 
   Do not dereference this pointer after this call.
   @retval 0 (success)
   @retval -errno (failure)
*/
int c2_net_end_point_put(struct c2_net_end_point *ep);

/**
    This enumeration describes the types of logical queues in a transfer
    machine.

    @note We use the term "queue" here to imply that the order of addition
    matters; in reality, external factors and buffer size will dictate
    the actual order in which a queued buffer operation gets initiated
    and when it gets completed.
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
   Event data structure. Only used in callback procedures used to
   convey completion of buffer operation, non-buffer related errors
   or diagnostic information.

   Buffer completion events include the following:
   - Successful termination of the operation.
   - Failure of the operation.
   - Expiry of the timeout period associated with the buffer.
 */
struct c2_net_event {
	/**
	   Indicates the type of buffer queue, or set to
	   C2_NET_QT_NR if not a buffer related event.
	 */
	enum c2_net_queue_type  nev_qtype;
	/**
	   Buffer pointer set if the queue type is not C2_NET_QT_NR.
	*/
	struct c2_net_buffer   *nev_buffer;

	/**
	   Time the event is posted.
	*/
	struct c2_time          nev_time;

	/**
	   Status or error code associated with the event.
	   A 0 implies success.  Typically negative error numbers are
	   used for failure.

	   When the value of c2_net_event.nev_qtype is C2_NET_QT_NR the 
	   callback is made to report non-buffer related errors,
	   or for diagnostic purposes.  
	   The value of this field is used to distinguish these cases:
	   - <b>Errors</b> The value of this field is a negative
	   integer. The following errors are well defined:
	   	- <b>-ENOBUFS</b> This indicates that the transfer machine
		lost messages due to a lack of receive buffers.
	   - <b>Diagnostic event</b> The value of this field is 0 or a
	   positive integer.
	   The c2_net_event.nev_payload field contains transport specific
	   diagnostic information.
	 */
	int                     nev_status;

	/**
	   Transport specific event descriptor (not necessarily
	   a pointer).
	   Transports could also choose to embed the event data structure
	   in a container structure appropriate to the event, which may
	   be of use to a diagnostic application.
	 */
	void                   *nev_payload;
};

/**
   A transfer machine is notified of events of interest with this subroutine.
   Typically, this is done by the transport associated with the transfer
   machine.
   @param tm Transfer machine pointer.
   @param ev Event pointer
 */
void c2_net_tm_event_post(struct c2_net_transfer_mc *tm,
			  struct c2_net_event *ev);


/**
   Callback function pointer type.
   @param tm Pointer to the transfer machine.
   @param ev Pointer to the event. The event data structure is
   released upon return from the subroutine.
*/
typedef void (*c2_net_tm_cb_proc_t)(struct c2_net_transfer_mc *tm,
				    struct c2_net_event *ev);

/**
 This data structure contains application callback function pointers.
 Multiple struct c2_net_transfer_mc objects can point to a single instance
 of such a structure.
 */
struct c2_net_tm_callbacks {
	/**
	   Optional callback for buffers on the C2_NET_QT_MSG_RECV queue.
	   Invoked when a message is received.
	*/
	c2_net_tm_cb_proc_t ntc_msg_recv_cb;

	/**
	   Optional callback for buffers on the C2_NET_QT_MSG_SEND queue.
	   Invoked when a message is sent.
	*/
	c2_net_tm_cb_proc_t ntc_msg_send_cb;

	/**
	   Optional callback for buffers on the C2_NET_QT_PASSIVE_BULK_RECV
	   queue.
	   Invoked when data has been written to the buffer on the completion
	   of a remotely initiated bulk transfer operation.
	 */
	c2_net_tm_cb_proc_t ntc_passive_bulk_recv_cb;

	/**
	   Optional callback for buffers on the C2_NET_QT_PASSIVE_BULK_SEND
	   queue.
	   Invoked when data has been read from the buffer on the completion
	   of a remotely initiated bulk transfer operation.
	 */
	c2_net_tm_cb_proc_t ntc_passive_bulk_send_cb;

	/**
	   Optional callback for buffers on the C2_NET_QT_ACTIVE_BULK_RECV
	   queue.
	   Invoked when data has been written to the buffer on the completion
	   of a locally initiated bulk transfer operation.
	 */
	c2_net_tm_cb_proc_t ntc_active_bulk_recv_cb;

	/**
	   Optional callback for buffers on the C2_NET_QT_ACTIVE_BULK_RECV
	   queue.
	   Invoked when data has been sent from the buffer on the completion
	   of a locally initiated bulk transfer operation.
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

	/** The number of failure events posted on buffers in the queue */
	uint64_t        nqs_num_f_events;

	/**
	    The total of time spent in the queue by all buffers
	    measured from when they were added to the queue
	    to the time their completion event got posted.
	 */
	struct c2_time  nqs_time_in_queue;

	/** The total number of bytes processed by buffers in the queue */
	uint64_t        nqs_total_bytes;

	/** The maximum number of bytes processed in a single
	    buffer in the queue 
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
	struct c2_net_tm_callbacks *ntm_callbacks;

	/** Application private field */
	void                       *ntm_cb_ctx;

	/** Set to true when the transfer machine is started */
	bool                        ntm_started;

	/** Lock. This has lower precedence than the lock in the
	    network domain.
	*/
	struct c2_rwlock            ntm_lock;

	/** Reference count tracking background activity such as
	    event callbacks and registered buffers.
	*/
	struct c2_ref               ntm_ref;

	/** Network domain pointer */
	struct c2_net_domain        ntm_dom;

	/** End point associated with this transfer machine.
	    It may have been provided by the application at start time,
	    or may be dynamically assigned. */
        struct c2_net_end_point    *ntm_ep;

	/** List of c2_net_end_point structures. Managed by the transport. */
	struct c2_list              ntm_end_points;

	/** List of c2_net_buffer structures involved in message passing.
	    Implements the C2_NET_QT_MSG_RECV and C2_NET_QT_MSG_SEND 
	    logical queues.
	*/
	struct c2_list              ntm_msg_bufs;

	/** List of c2_net_buffer structures involved in passive bulk transfer.
	    Implements the C2_NET_QT_PASSIVE_BULK_SEND and 
	    C2_NET_QT_PASSIVE_BULK_RECV logical queues.
	*/
	struct c2_list              ntm_passive_bufs;

	/** List of c2_net_buffer structures involved in active bulk transfer.
	    Implements the C2_NET_QT_ACTIVE_BULK_RECV and 
	    C2_NET_QT_ACTIVE_BULK_SEND logical queues.
	*/
	struct c2_list              ntm_active_bufs;

	/** Statistics maintained per logical queue */
	struct c2_net_qstats        ntm_qstats[C2_NET_QT_NR];


	/** Domain linkage */
	struct c2_list_link         ntm_linkage;

	/** Transport private data */
        void                       *ntm_xprt_private;
};

/**
   Initialize a transfer machine.
   @param tm  Transfer machine pointer.
   @param dom Network domain pointer.
   @retval 0 (success)
   @retval -errno (failure)
*/
int c2_net_tm_init(struct c2_net_transfer_mc *tm, struct c2_net_domain *dom);

/**
   Finalize a transfer machine. Will fail if there are still registered
   buffers.
   @param tm Transfer machine pointer.
   @retval 0 (success)
   @retval -errno (failure)
*/
int c2_net_tm_fini(struct c2_net_transfer_mc *tm);

/**
   Start a transfer machine.  Optionally specify an end point to be
   associated with the transfer machine - servers have well defined
   end points and would need to use this option.
   @param tm  Transfer machine pointer.
   @param ep  Optional end point to associate with the transfer machine.
   Specify NULL if the end point is to be dynamically assigned.
   @retval 0 (success)
   @retval -errno (failure)
*/
int c2_net_tm_start(struct c2_net_transfer_mc *tm, 
		    struct c2_net_end_point *ep);

/**
   Retrieve transfer machine statistics for all or for a single logical queue,
   optionally resetting the data.  The operation is performed atomically
   with respect to on-going transfer machine activity.
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
int c2_net_stats_get(struct c2_net_transfer_mc *tm, 
		     enum c2_net_queue_type qtype,
		     struct c2_net_qstats *qs,
		     bool reset);

/**
   This data structure is used to track the memory used
   for message passing or bulk data transfer over the network.
   
   Support for scatter-gather buffers is provided by use of a c2_bufvec;
   upper layer protocols may impose limitations on the
   use of scatter-gather, especially for message passing.
   The transport will impose limitations on the number of vector elements
   and the overall maximum buffer size.

   The invoking application is responsible for the creation
   of these data structures.
   As such, memory alignment considerations are handled by the 
   invoking application.
 */
struct c2_net_buffer {
	/**
	   Vector pointing to memory associated with this data structure.
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
	size_t                     nb_length;

	/**
	   Transfer machine pointer.  
	   The application should set this value before registering the buffer.
	*/
	struct c2_net_transfer_mc *nb_tm;

	/**
	   The logical queue identifier.
	   The application should set this value before registering the buffer.
	   @note The logical queue choice may not be changed until after
	   the buffer is deregistered.
	*/
        enum c2_net_queue_type     nb_qtype;

	/**
	   Application specific contextual information.
	*/
	void                      *nb_ctx;

	/**
	   Absolute time by which an operation involving the buffer should
	   stop with failure if not completed.
	   <b>Support for this is transport specific.</b>
	   Set the value to 0 to disable the timeout.
	*/
	struct c2_time             nb_timeout;

	/**
	   Time at which the buffer was added to its logical queue.
	*/
	struct c2_time             nb_add_time;

	/**
	   Set to true when the buffer is registered with the transport.
	*/
	bool                       nb_registered;

	/**
	   Network transport descriptor set when the buffer is added to
	   the C2_NET_QT_PASSIVE_BULK_RECV or C2_NET_QT_PASSIVE_BULK_SEND
	   queues.

	   Applications should convey the descriptor to the active side
	   to perform the bulk data transfer.

	   Applications are responsible for freeing the memory used by
	   this descriptor with the c2_net_desc_free() subroutine.
	*/
	struct c2_net_buf_desc     nb_desc;

	/**
	   This field identifies an end point. Its usage varies by context:

	   - In received messages (C2_NET_QT_MSG_RECV queue) the transport
	   will set the end point to identify the sender of the message
	   before posting the associated event.
	   The end point will be released when the event callback returns.
	   - When sending messages 
	   the application should specify the end point of the destination
	   before adding the buffer to the C2_NET_QT_MSG_SEND queue.

	   The field is not used at other times.
	*/
	struct c2_net_end_point   *nb_ep;

	/**
	   Linkage into one of the transfer machine lists that implement the
	   logical queues.
	*/
	struct c2_list_link        nb_linkage;

	/**
	   Transport private data associated with the buffer.
	   Will be freed when the buffer is deregistered, if not earlier.
	*/
        void                      *nb_xprt_private;
};

/**
   Register a buffer for a specific use with a transfer machine.
   @param buf Pointer to a buffer. The buffer should have the following fields
   initialized:
   - c2_net_buffer.nb_tm should be set to point to the transfer machine
   - c2_net_buffer.nb_qtype should be set to identify the logical queue
   on which the buffer will be used.
   - c2_net_buffer.nb_buffer should be initialized to point to the buffer
   memory regions.
   @param buf Specify the buffer pointer.
   @retval 0 (success)
   @retval -errno (failure)
*/
int c2_net_buffer_register(struct c2_net_buffer *buf);

/**
   Deregisters a previously registered buffer and releases any transport
   specific resources associated with it.
   Any outstanding use of the buffer is cancelled.
   @param buf Specify the buffer pointer.
   @retval 0 (success)
   @retval -errno (failure)
*/
int c2_net_buffer_deregister(struct c2_net_buffer *buf);

/**
   Add a registered buffer to its associated transfer machine logical queue.
   - Buffers added to the C2_NET_QT_MSG_RECV queue are used to receive
   messages.
   - Buffers added to the C2_NET_QT_MSG_SEND queue must contain a message
   to be sent. The c2_net_buffer.nb_ep field must identify the destination,
   and the c2_net_buffer.nb_length field must be set to the actual message
   length.
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
   - Buffers added to the C2_NET_PASSIVE_BULK_SEND and
   C2_NET_ACTIVE_BULK_SEND queues must set the c2_net_buffer.nb_length
   field to the actual length of the data to be transferred.

   The buffer should not be referenced until the operation completion
   callback is invoked for the buffer.
   @param buf Specify the buffer pointer.
   @retval 0 (success)
   @retval -errno (failure)
*/
int c2_net_buffer_add(struct c2_net_buffer *buf);

/**
   Remove a registered buffer from a logical queue, if possible,
   cancelling any operation in progess.
   It is not guaranteed that this operation will always succeed, and
   there are race conditions in the execution of this request and the
   concurrent invocation of the completion callback on the buffer.
   @param buf Specify the buffer pointer.
   @retval 0 (success)
   @retval -errno (failure)
*/
int c2_net_buffer_del(struct c2_net_buffer *buf);

/**
   Copy a network buffer descriptor.
   @param from_desc Specifies the source descriptor data structure.
   @param to_desc Specifies the destination descriptor data structure.
   @retval 0 (success)
   @retval -errno (failure)
*/
int c2_net_desc_copy(struct c2_net_buf_desc *from_desc,
		     struct c2_net_buf_desc *to_desc);

/**
   Free a network buffer descriptor.
   @param desc Specify the network buffer descriptor. Its fields will be
   cleared after this operation.
   @retval 0 (success)
   @retval -errno (failure)
*/
int c2_net_desc_free(struct c2_net_buf_desc *desc);
		     
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
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
