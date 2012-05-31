/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Alexey Lyashkov <Alexey_Lyashkov@xyratex.com>,
 *                  Nikita Danilov <Nikita_Danilov@xyratex.com>,
 *                  Carl Braganza <Carl_Braganza@xyratex.com>
 * Original creation date: 04/01/2010
 */

#ifndef __COLIBRI_NET_NET_H__
#define __COLIBRI_NET_NET_H__

#include <stdarg.h>

#include "lib/cdefs.h"
#include "lib/rwlock.h"
#include "lib/list.h"
#include "lib/tlist.h"
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
   @li Network buffer event (c2_net_buffer_event);
   @li Network domain (c2_net_domain);
   @li Network end point (c2_net_end_point);
   @li Network transfer machine (c2_net_transfer_mc);
   @li Network transfer machine event (c2_net_tm_event);
   @li Network transport (c2_net_xprt);

   See <a href="https://docs.google.com/a/xyratex.com/document/d/1tm_IfkSsW6zfOxQlPMHeZ5gjF1Xd0FAUHeGOaNpUcHA/edit?hl=en#">RPC Bulk Transfer Task Plan</a>
   for details on the design and use of this API.  If you are writing a
   transport, then the document is the reference for the internal threading and
   serialization model.

   See <a href="https://docs.google.com/a/xyratex.com/document/d/1TZG__XViil3ATbWICojZydvKzFNbL7-JJdjBbXTLgP4/edit?hl=en_US">HLD of Colibri LNet Transport</a>
   for additional details on the design and use of this API.

   @{

 */

/* import */
struct c2_bitmap;

/* export */
struct c2_net_xprt;
struct c2_net_xprt_ops;
struct c2_net_domain;
struct c2_net_transfer_mc;
struct c2_net_tm_event;
struct c2_net_tm_callbacks;
struct c2_net_end_point;
struct c2_net_buffer;
struct c2_net_buf_desc;
struct c2_net_buffer_event;
struct c2_net_buffer_callbacks;
struct c2_net_qstats;

/**
 constructor for the network library
 */
int c2_net_init(void);

/**
 destructor for the network library.
 release all allocated resources
 */
void c2_net_fini(void);

enum {
	/* Hex value for "NET_DOM" */
	C2_NET_DOMAIN_MAGIX = 0x4E45545F444F4D
};

/** Network transport (e.g. lnet) */
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
	   Initialises transport specific part of a domain (e.g., start threads,
	   initialise portals).
	   Only the c2_net_mutex is held across this call.
	 */
	int  (*xo_dom_init)(struct c2_net_xprt *xprt,
			    struct c2_net_domain *dom);
	/**
	   Finalises transport resources in a domain.
	   Only the c2_net_mutex is held across this call.
	 */
	void (*xo_dom_fini)(struct c2_net_domain *dom);

	/**
	   Performs transport level initialization of the transfer machine.
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
	   Optional method to set the processor affinity for the threads of
	   a transfer machine.
	   The transfer machine must be initialized but not yet started.
	   @param tm Transfer machine pointer.
	   @param processors Processor bitmap.
	   @retval 0 (success)
	   @retval -ENOSYS  No affinity support available. Implied by a
	   missing method.
	 */
	int (*xo_tm_confine)(struct c2_net_transfer_mc *tm,
			     const struct c2_bitmap *processors);

	/**
	   Initiates the startup of the (initialized) transfer machine.
	   A completion event should be posted when started, using a different
	   thread.
	   <b>Serialized using the transfer machine mutex.</b>
	   @param tm   Transfer machine pointer.
             The following fields are of special interest to this method:
             @li ntm_dom
	     @li ntm_xprt_private
	   @param addr  Address of the transfer machine.
	   The method should not reference this string after it returns.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_tm_start()
	 */
	int (*xo_tm_start)(struct c2_net_transfer_mc *tm, const char *addr);

	/**
	   Initiates the shutdown of a transfer machine, cancelling any
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
	   Releases resources associated with a transfer machine.
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
	   Creates an end point with a specific address.
	   @param epp     Returned end point data structure.
	   @param tm      Specify the transfer machine pointer.
	   @param addr    Address string.  Could be NULL to
	                  indicate dynamic addressing.
			  Do not reference the string after return.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_end_point_create()
	 */
	int (*xo_end_point_create)(struct c2_net_end_point **epp,
				   struct c2_net_transfer_mc *tm,
				   const char *addr);

	/**
	   Registers the buffer for use with a transfer machine in
	   the manner indicated by the c2_net_buffer.nb_qtype value.
	   @param nb  Buffer pointer with c2_net_buffer.nb_dom set.
           @retval 0 (success)
	   @retval -errno (failure)
	   @see c2_net_buffer_register()
	 */
	int (*xo_buf_register)(struct c2_net_buffer *nb);

	/**
	   Deregisters the buffer from the transfer machine.
	   @param nb  Buffer pointer with c2_net_buffer.nb_tm set.
	   @see c2_net_buffer_deregister()
	 */
	void (*xo_buf_deregister)(struct c2_net_buffer *nb);

	/**
	   Initiates an operation on a buffer on the transfer machine's
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
	   Cancels an operation involving a buffer.
	   The method should cancel the operation involving use of the
	   buffer, as described by the value of the c2_net_buffer.nb_qtype
	   field.
	   The C2_NET_BUF_CANCELLED flag should be set in buffers whose
	   operations get cancelled, so c2_net_buffer_event_post() can
	   enforce the right error status.
	   <b>Serialized using the transfer machine mutex.</b>
	   @param nb  Buffer pointer with c2_net_buffer.nb_tm and
	   c2_net_buffer.nb_qtype set.
	   @see c2_net_buffer_del()
	 */
	void (*xo_buf_del)(struct c2_net_buffer *nb);

	/**
	   Invoked by the c2_net_buffer_event_deliver_synchronously()
	   subroutine to request the transport to disable automatic delivery
	   of buffer events.  The method is optional and need not be specified
	   if this support is not available.
	   If supported, then the xo_bev_deliver_all() and the xo_bev_pending()
	   operations must be provided.
	   @see c2_net_buffer_event_deliver_synchronously()
	 */
	int  (*xo_bev_deliver_sync)(struct c2_net_transfer_mc *tm);

	/**
	   Invoked by the c2_net_buffer_event_deliver_all() subroutine.
	   Optional if the synchronous buffer event delivery feature is not
	   supported.
	   As buffer event delivery takes place without holding the transfer
	   machine mutex, the transport should protect the invocation of this
	   subroutine from synchronous termination of the transfer machine.
	 */
	void (*xo_bev_deliver_all)(struct c2_net_transfer_mc *tm);

	/**
	   Invoked by the c2_net_buffer_event_pending() subroutine.
	   Optional if the synchronous buffer event delivery feature is not
	   supported.
	 */
	bool (*xo_bev_pending)(struct c2_net_transfer_mc *tm);

	/**
	   Invoked by the c2_net_buffer_event_notify() subroutine.
	   Optional if the synchronous buffer event delivery feature is not
	   supported.
	 */
	void (*xo_bev_notify)(struct c2_net_transfer_mc *tm,
			      struct c2_chan *chan);

	/**
	   Retrieves the maximum buffer size (includes all segments).
	   @param dom     Domain pointer.
	   @retval size    Returns the maximum buffer size.
	   @see c2_net_domain_get_max_buffer_size()
	 */
	c2_bcount_t (*xo_get_max_buffer_size)(const struct c2_net_domain *dom);

	/**
	   Retrieves the maximum buffer segment size.
	   @param dom     Domain pointer.
	   @retval size    Returns the maximum segment size.
	   @see c2_net_domain_get_max_buffer_segment_size()
	 */
	c2_bcount_t (*xo_get_max_buffer_segment_size)(const struct c2_net_domain
						      *dom);

	/**
	   Retrieves the maximum number of buffer segments.
	   @param dom      Domain pointer.
	   @retval num_segs Returns the maximum segment size.
	   @see c2_net_domain_get_max_buffer_segment_size()
	 */
	int32_t (*xo_get_max_buffer_segments)(const struct c2_net_domain *dom);
};

/**
 Initializes the transport software.
 A network domain must be initialized to use the transport.
 @param xprt Tranport pointer.
 @retval 0 (success)
 @retval -errno (failure)
 */
int  c2_net_xprt_init(struct c2_net_xprt *xprt);

/**
 Shuts down the transport software.
 All associated network domains should be cleaned up at this point.
 @pre Network domains should have been finalized.
 @param xprt Tranport pointer.
 */
void c2_net_xprt_fini(struct c2_net_xprt *xprt);

/** @}
 @addtogroup net
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
	   List of c2_net_buffer structures registered with the domain.
	 */
	struct c2_list      nd_registered_bufs;

	/**
	   List of c2_net_transfer_mc structures.
	 */
	struct c2_list      nd_tms;

	/** Transport private domain data. */
	void               *nd_xprt_private;

	/** Pointer to transport */
	struct c2_net_xprt *nd_xprt;

	/** ADDB context for events related to this domain */
	struct c2_addb_ctx  nd_addb;

        /** Linkage for invoking application */
        struct c2_tlink     nd_app_linkage;

	/** Network magic */
	uint64_t            nd_magic;

        /** Transfer machine pool colour counter */
        int                 nd_pool_colour_counter;
};

/**
   Initializes a domain.
 @param dom Domain pointer.
 @param xprt Tranport pointer.
 @pre dom->nd_xprt == NULL
 @retval 0 (success)
 @retval -errno (failure)
 */
int c2_net_domain_init(struct c2_net_domain *dom, struct c2_net_xprt *xprt);

/**
   Releases resources related to a domain.
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
   structure is managed by the network transport component and is associated
   with the transfer machine that created the structure.

   Multiple entities may reference and use the data structure at the same time,
   so a reference count is maintained within it to determine when it is safe to
   release the structure.

   Transports should embed this data structure in their private end point
   structures, and provide the release() method required to free them.
   The release() method, which is called with the transfer machine mutex
   locked, should remove the data structure from the transfer machine
   ntm_end_points list.
 */
struct c2_net_end_point {
	/** Keeps track of usage */
	struct c2_ref              nep_ref;
	/** Pointer to transfer machine */
	struct c2_net_transfer_mc *nep_tm;
	/** Linkage in the transfer machine list */
	struct c2_list_link        nep_tm_linkage;
	/** Transport specific printable representation of the
	    end point address.
	 */
	const char                *nep_addr;
};

/**
   Allocates an end point data structure representing the desired
   end point and sets its reference count to 1,
   or increments the reference count of an existing matching data structure.
   The data structure is linked to the transfer machine.
   The invoker should call the c2_net_end_point_put() when the
   data structure is no longer needed.
   @param epp Pointer to a pointer to the data structure which will be
   set upon return.  The reference count of the returned data structure
   will be at least 1.
   @param tm  Transfer machine pointer.  The transfer machine must be in
   the started state.
   @param addr String describing the end point address in a transport specific
   manner.  The format of this address string is the same as the printable
   representation form stored in the end point nep_addr field.  It is optional,
   and if NULL, the transport may support assignment of an end point with a
   dynamic address; however this is not guaranteed.
   The address string, if specified, is not referenced again after return from
   this subroutine.
   @see c2_net_end_point_get(), c2_net_end_point_put()
   @pre tm->ntm_state == C2_NET_TM_STARTED
   @post (*epp)->nep_ref->ref_cnt >= 1 && (*epp)->nep_addr != NULL &&
   (*epp)->nep_tm == tm
   @retval 0 on success
   @retval -errno on failure
 */
int c2_net_end_point_create(struct c2_net_end_point  **epp,
			    struct c2_net_transfer_mc *tm,
			    const char                *addr);

/**
   Increments the reference count of an end point data structure.
   This is used to safely point to the structure in a different context -
   when done, the reference count should be decremented by a call to
   c2_net_end_point_put().

   @param ep End point data structure pointer.
   @pre ep->nep_ref->ref_cnt >= 1
 */
void c2_net_end_point_get(struct c2_net_end_point *ep);

/**
   Decrements the reference count of an end point data structure.
   The structure will be released when the count goes to 0.
   @param ep End point data structure pointer.
   Do not dereference this pointer after this call.
   @pre ep->nep_ref->ref_cnt >= 1
   @note The transfer machine mutex will be obtained internally to synchronize
   the transport provided release() method in case the end point gets released.
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
	   remotely initiated bulk data send operations
	   that will read from these buffers.
	 */
	C2_NET_QT_PASSIVE_BULK_RECV,

	/**
	   Queue with buffers awaiting completion of
	   remotely initiated bulk data receive operations
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
   Transfer machine event types are defined by this enumeration.
 */
enum c2_net_tm_ev_type {
	C2_NET_TEV_ERROR=0,        /**< General error */
	C2_NET_TEV_STATE_CHANGE,   /**< Transfer machine state change event */
	C2_NET_TEV_DIAGNOSTIC,     /**< Diagnostic event */
	C2_NET_TEV_NR
};

/**
   Data structure used to provide asynchronous notification of
   significant events, such as the completion of buffer operations,
   transfer machine state changes and general errors.

   All events have the following fields set:
   - nte_type
   - nte_tm
   - nte_time
   - nte_status

   The nte_type field should be referenced to determine the type of
   event, and which other fields of this structure get set:

   - C2_NET_TEV_ERROR provides error notification, out of the context of
     any buffer operation completion, or a transfer machine state change.
     No additional fields are set.
   - C2_NET_TEV_STATE_CHANGE provides notification of a transfer machine
     state change.
     The nte_next_state field describes the destination state.
     Refer to the nte_status field to determine if the operation succeeded.
     The nte_ep field is set if the next state is C2_NET_TM_STARTED; the
     value is used to set the ntm_ep field of the transfer machine.
   - C2_NET_TEV_DIAGNOSTIC provides diagnostic information.
     The nte_payload field may point to transport specific data.
     The API does not require nor specify how a transport produces
     diagnostic information, but does require that diagnostic events
     not be produced unless explicitly requested.

   This data structure is typically assigned on the stack of the thread
   that invokes the c2_net_tm_event_post() subroutine.  Applications
   should not attempt to save a reference to it from their callback
   functions.

   @see c2_net_tm_event_post() for details on event delivery concurrency.
 */
struct c2_net_tm_event {
	/**
	   Indicates the type of event.
	   Other fields get set depending on the value of this field.
	 */
	enum c2_net_tm_ev_type     nte_type;

	/**
	   Transfer machine pointer.
	 */
	struct c2_net_transfer_mc *nte_tm;

	/**
	   Time the event is posted.
	 */
	c2_time_t                  nte_time;

	/**
	   Status or error code associated with the event.

	   In all event types other than C2_NET_TEV_DIAGNOSTIC, a 0 in this
	   field implies successful completion, and a negative error number
	   is used to indicate the reasons for failure.
	   The following errors are well defined:
		- <b>-ENOBUFS</b> This indicates that the transfer machine
		lost messages due to a lack of receive buffers.

	   Diagnostic events are free to make any use of this field.
	 */
	int32_t                    nte_status;

	/**
	   Valid only if the nte_type is C2_NET_TEV_STATE_CHANGE.

	   The next state of the transfer machine is set in this field.
	   Any associated error condition defined by the nte_status field.
	 */
	enum c2_net_tm_state       nte_next_state;

	/**
	   End point pointer to be used to set the value of the ntm_ep
	   field when the state changes to C2_NET_TM_STARTED.
	*/
	struct c2_net_end_point   *nte_ep;

	/**
	   Valid only if the nte_type is C2_NET_TEV_STATE_DIAGNOSTIC.

	   Transports may use this to point to internal data; they
	   could also choose to embed the event data structure
	   in a transport specific structure appropriate to the event.
	   Either approach would be of use to a diagnostic application.
	 */
	void                      *nte_payload;
};

/**
   Callbacks associated with a transfer machine.
   Multiple transfer machines can reference an instance of this structure.
 */
struct c2_net_tm_callbacks {
	/**
	   Event callback.
	   @param ev Pointer to the transfer machine event. The pointer
	   is not valid after return from the subroutine.
	 */
	void (*ntc_event_cb)(const struct c2_net_tm_event *ev);
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
	 */
	struct c2_mutex             ntm_mutex;

	/**
	   Callback activity is tracked by this counter.
	   It is incremented by c2_net_tm_post_event() before invoking
	   a callback, and decremented when it returns.
	 */
	uint32_t                    ntm_callback_counter;

	/** Network domain pointer */
	struct c2_net_domain       *ntm_dom;

	/**
	   List of c2_net_end_point structures. Managed by the transport.
	 */
	struct c2_list              ntm_end_points;

	/**
	   End point associated with this transfer machine.
	   Messages sent from this
	   transfer machine appear to have originated from this end point.

	   It is created internally with the address provided in the
	   call to c2_net_tm_start().  The field is set only upon
	   successful start of the transfer machine.
	   The field is cleared durinng fini.
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
	struct c2_tl		    ntm_q[C2_NET_QT_NR];

	/** Statistics maintained per logical queue */
	struct c2_net_qstats        ntm_qstats[C2_NET_QT_NR];

	/** ADDB context for events related to this transfer machine */
	struct c2_addb_ctx          ntm_addb;

	/** Domain linkage */
	struct c2_list_link         ntm_dom_linkage;

        /**
         * Transfer machine colour. It is used to get
         * buffer from buffer pool.
         */
        int                         ntm_pool_colour;

	/** Transport private data */
        void                       *ntm_xprt_private;

	/** Indicates if automatic delivery of buffer events will take place. */
	bool                        ntm_bev_auto_deliver;
};

/**
   Initializes a transfer machine.
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
   @post tm->ntm_bev_auto_deliver is set.
   @retval 0 (success)
   @retval -errno (failure)
 */
int c2_net_tm_init(struct c2_net_transfer_mc *tm, struct c2_net_domain *dom);

/**
   Finalizes a transfer machine, releasing any associated
   transport specific resources.

   All application references to end points associated with this transfer
   machine should be released prior to this call.
   @pre
   (tm->ntm_state == C2_NET_TM_STOPPED ||
    tm->ntm_state == C2_NET_TM_FAILED  ||
    tm->ntm_state == C2_NET_TM_INITIALIZED) &&
   ((c2_list_is_empty(&tm->ntm_end_points) && tm->ntm_ep == NULL) ||
    (c2_list_length(&tm->ntm_end_points) == 1 &&
     c2_list_contains(&tm->ntm_end_points, tm->ntm_ep) &&
     c2_atomic64_get(tm->ntm_ep->nep_ref.ref_cnt) == 1))
   @param tm Transfer machine pointer.
 */
void c2_net_tm_fini(struct c2_net_transfer_mc *tm);

/**
   Sets the processor affinity of the threads of a transfer machine.
   The transfer machine must be initialized but not yet started.

   Support for this operation is transport specific.
   @pre tm->ntm_state == C2_NET_TM_INITIALIZED
   @param tm Transfer machine pointer.
   @param processors Processor bitmap.  The bit map is not referenced
   internally after the subroutine returns.
   @retval -ENOSYS  No affinity support available in the transport.
   @see @ref Processor "Processor API"
   @see @ref bitmap "Bitmap API"
 */
int c2_net_tm_confine(struct c2_net_transfer_mc *tm,
		      const struct c2_bitmap *processors);

/**
   Starts a transfer machine.

   The subroutine does not block the invoker. Instead the state is
   immediately changed to C2_NET_TM_STARTING, and an event will be
   posted to indicate when the transfer machine has transitioned to
   the C2_NET_TM_STARTED state.

   @note It is possible that the state change event be posted before this
   subroutine returns.
   It is guaranteed that the event will be posted on a different thread.

   @pre tm->ntm_state == C2_NET_TM_INITIALIZED
   @param tm  Transfer machine pointer.
   @param addr End point address to associate with the transfer machine.
   May be null if dynamic addressing is supported by the transport.
   The end point is created internally and made visible by the ntm_ep field
   only if the start operation succeeds.
   @retval 0 (success)
   @retval -errno (failure)
   @see c2_net_end_point_create()
 */
int c2_net_tm_start(struct c2_net_transfer_mc *tm, const char *addr);

/**
   Initiates the shutdown of a transfer machine.  New messages will
   not be accepted and new end points cannot be created.
   Pending operations will be completed or aborted as desired.

   All end point references must be released by the application prior
   to invocation.  The only end point reference that may exist is that of
   this transfer machine itself, and that will be released during fini.

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
   Retrieves transfer machine statistics for all or for a single logical queue,
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
   A transfer machine is notified of non-buffer related events of interest
   with this subroutine.
   Typically, the subroutine is invoked by the transport associated with
   the transfer machine.

   The event data structure is not referenced from
   elsewhere after this subroutine returns, so may be allocated on the
   stack of the calling thread.

   Multiple concurrent events may be delivered for a given transfer machine.

   The subroutine will also signal to all waiters on the
   c2_net_transfer_mc.ntm_chan field after delivery of the callback.

   The invoking process should be aware that the callback subroutine could
   end up making re-entrant calls to the transport layer.

   @param ev Event pointer. The nte_tm field identifies the transfer machine.

   @see c2_net_tm_buffer_post()
 */
void c2_net_tm_event_post(const struct c2_net_tm_event *ev);

/**
   Buffer completion events are described by this data structure.
 */
struct c2_net_buffer_event {
	/** Pointer to the buffer */
	struct c2_net_buffer      *nbe_buffer;

	/**
	   Time the event is posted.
	 */
	c2_time_t                  nbe_time;

	/**
	   Status or error code associated with the event.
	   A 0 in this field implies successful completion, and a negative
	   error number is used to indicate the reasons for failure.

	   The following errors are well defined:
		- <b>-ECANCELED</b> This is used in buffer release events to
		indicate that the associated buffer operation was
		cancelled by a call to c2_net_buffer_del().
		- <b>-ETIMEDOUT</b> This is used in buffer release events to
		indicate that the associated buffer operation did not complete
		before the current time exceeded the nb_timeout value.
		The support for this feature is transport specific.
		The nb_timeout value is always reset to C2_TIME_NEVER by the
		time the buffer callback is invoked.
	 */
	int32_t                    nbe_status;

	/**
	   Length of the buffer data associated with this event.
	   The field is valid only if the event is posted
	   for the C2_NET_QT_MSG_RECV, C2_NET_QT_PASSIVE_BULK_RECV or
	   C2_NET_QT_ACTIVE_BULK_RECV queues.
	 */
	c2_bcount_t                nbe_length;

	/**
	   Starting offset of the buffer data associated with this event,
	   if the event is posted for the
	   C2_NET_QT_MSG_RECV, C2_NET_QT_PASSIVE_BULK_RECV or
	   C2_NET_QT_ACTIVE_BULK_RECV queues.

	   Provided for future support of multi-delivery buffer transports.
	   Applications should take it into consideration when determining the
	   starting location of the event data in the buffer.
	 */
	c2_bcount_t                nbe_offset;

	/**
	   This field is used only in successful completion of buffers
	   in the received message queue (C2_NET_QT_MSG_RECV).
	   The transport will set the end point to identify the sender
	   of the message before invoking the completion callback on the buffer.

	   The end point will be released when the callback returns, so
	   applications should increment the reference count on the end
	   point with c2_net_end_point_get(), if they wish to dereference
	   the pointer in a different context.
	 */
	struct c2_net_end_point   *nbe_ep;
};

/**
   Buffer callback function pointer type.
   @param ev Pointer to the buffer event. The pointer is not valid after
   the callback returns.
 */
typedef void (*c2_net_buffer_cb_proc_t)(const struct c2_net_buffer_event *ev);

/**
   This data structure contains application callback function pointers
   for buffer completion callbacks, one function per type of buffer queue.

   Applications should provide a pointer to an instance of such a
   structure in the nb_callbacks field of the struct c2_net_buffer.
   Multiple objects can point to a single instance of such a structure.

   @see c2_net_buffer_event_post() for the concurrency semantics.
 */
struct c2_net_buffer_callbacks {
	c2_net_buffer_cb_proc_t nbc_cb[C2_NET_QT_NR];
};

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
	/** Indicates that the buffer operation has been cancelled */
	C2_NET_BUF_CANCELLED   = 1<<3,
	/** Indicates that the buffer operation has timed out */
	C2_NET_BUF_TIMED_OUT   = 1<<4,
	/** Set by the transport to indicate that a buffer should not be
	    dequeued in a c2_net_buffer_event_post() call.
	 */
	C2_NET_BUF_RETAIN      = 1<<5,
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
	   The actual amount of data to be transferred in the case of adding
	   the buffer to the C2_NET_QT_MSG_SEND, C2_NET_QT_PASSIVE_BULK_SEND or
	   C2_NET_QT_ACTIVE_BULK_SEND queues.

	   The actual amount of valid data received, upon completion of
	   C2_NET_QT_MSG_RECV, C2_NET_QT_PASSIVE_BULK_RECV or
	   C2_NET_QT_ACTIVE_BULK_RECV queue operations, is not set here, but in
	   the nbe_length field of the c2_net_buffer_event instead.
	 */
	c2_bcount_t                nb_length;

	/**
	   The starting offset in the buffer from which the data should
	   be read, in the case of adding a buffer to the
	   C2_NET_QT_MSG_SEND, C2_NET_QT_PASSIVE_BULK_SEND or
	   C2_NET_QT_ACTIVE_BULK_SEND queues.

	   It is transport specific if a non-zero value is supported.
	 */
	c2_bcount_t                nb_offset;

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
	    prior to adding the buffer to a transfer machine queue.
	 */
	const struct c2_net_buffer_callbacks *nb_callbacks;

	/**
	   Absolute time by which an operation involving the buffer should
	   stop with failure if not completed.
	   The application should set this field prior to adding the
	   buffer to a transfer machine logical queue.

	   <b>Support for this is transport specific.</b>
	   A value of C2_TIME_NEVER disables the timeout.
	   The value is forced to C2_TIME_NEVER during buffer registration,
	   and reset to the same prior to the invocation of the buffer
	   callback so applications need not bother with this field unless
	   they intend to set a timeout value.

	   Adding a buffer to a logical queue will fail with a -ETIME
	   error code if the specified nb_timeout value is in the past.
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
	   This field identifies an end point in the associated transfer
	   machine.

	   When sending messages the application should specify the end point
	   of the destination before adding the buffer to the
	   C2_NET_QT_MSG_SEND queue.

	   The field is not used for the bulk cases nor for received messages.
	 */
	struct c2_net_end_point   *nb_ep;

	/**
	   Linkage into one of the transfer machine lists that implement the
	   logical queues.
	   There is only one linkage for all of the queues, as a buffer
	   can only be used for one type of operation at a time.

	   It is also used for linkage into c2_net_buffer_pool::nbp_colours[].
	   The application should not modify this field.
	 */
	struct c2_tlink		   nb_tm_linkage;

	/** Linkage into a network buffer pool. */
	struct c2_tlink		   nb_lru;

        /* This link is used by I/O service */
        struct c2_tlink            nb_ioservice_linkage;

	/** Magic for network buffer list. */
	uint64_t		   nb_magic;

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
	   Application specific private data associated with the buffer.
	   It is populated and used by the end user.
	   It is end user's responsibility to use this field to allocate
	   or deallocate any memory regions stored in this field.

	   It is neither verified by net code nor do the net layer
	   invariants touch it.
	 */
	void			  *nb_app_private;

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
	   Minimum remaining size in a receive buffer to allow reuse
	   for multiple messages.
	   The value may not be 0 for buffers in the C2_NET_QT_MSG_RECV queue.
	 */
	c2_bcount_t                nb_min_receive_size;

	/**
	   Maximum number of messages that may be received in the buffer.
	   The value may not be 0 for buffers in the C2_NET_QT_MSG_RECV queue.
	 */
	uint32_t                   nb_max_receive_msgs;

	/** ADDB context for events related to this buffer */
	struct c2_addb_ctx         nb_addb;
};

/**
   Registers a buffer with the domain. The domain could perform some
   optimizations under the covers.
   @pre
(buf->nb_flags == 0) &&
(buf->nb_buffer.ov_buf != NULL) &&
c2_vec_count(&buf->nb_buffer.ov_vec) > 0
   @post ergo(result == 0, buf->nb_flags & C2_NET_BUF_REGISTERED)
   @post ergo(result == 0, buf->nb_timeout == C2_TIME_NEVER)
   @param buf Pointer to a buffer. The buffer should have the following fields
   initialized:
   - c2_net_buffer.nb_buffer should be initialized to point to the buffer
   memory regions.
   The buffer's timeout value is initialized to C2_TIME_NEVER upon return.
   @param dom Pointer to the domain.
   @retval 0 (success)
   @retval -errno (failure)
 */
int c2_net_buffer_register(struct c2_net_buffer *buf,
			   struct c2_net_domain *dom);

/**
   Deregisters a previously registered buffer and releases any transport
   specific resources associated with it.
   The buffer should not be in use, nor should this subroutine be
   invoked from a callback.
   @pre
(buf->nb_flags == C2_NET_BUF_REGISTERED) &&
(buf->nb_dom == dom)
   @param buf Specify the buffer pointer.
   @param dom Specify the domain pointer.
 */
void c2_net_buffer_deregister(struct c2_net_buffer *buf,
			      struct c2_net_domain *dom);

/**
   Adds a registered buffer to a transfer machine's logical queue specified
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
   - The callback function pointer for the appropriate queue type
   must be set in nb_callbacks.

   The buffer should not be modified until the operation completion
   callback is invoked for the buffer.

   The buffer completion callback is guaranteed to be invoked on a
   different thread.

   @pre
(buf->nb_dom == tm->ntm_dom) &&
(tm->ntm_state == C2_NET_TM_STARTED) &&
c2_net__qtype_is_valid(buf->nb_qtype) &&
buf->nb_flags == C2_NET_BUF_REGISTERED &&
buf->nb_callbacks->nbc_cb[buf->nb_qtype] != NULL &&
ergo(buf->nb_qtype == C2_NET_QT_MSG_RECV,
     buf->nb_min_receive_size != 0 && buf->nb_max_receive_msgs != 0) &&
ergo(buf->nb_qtype == C2_NET_QT_MSG_SEND, buf->nb_ep != NULL) &&
ergo(buf->nb_qtype == C2_NET_QT_ACTIVE_BULK_RECV ||
     buf->nb_qtype == C2_NET_QT_ACTIVE_BULK_SEND, buf->nb_desc.nbd_len != 0) &&
ergo(buf->nb_qtype == C2_NET_QT_MSG_SEND ||
     buf->nb_qtype == C2_NET_QT_PASSIVE_BULK_SEND ||
     buf->nb_qtype == C2_NET_QT_ACTIVE_BULK_SEND, buf->nb_length > 0)
   @param buf Specify the buffer pointer.
   @param tm  Specify the transfer machine pointer
   @retval 0 (success)
   @retval -errno (failure)
   @retval -ETIME nb_timeout is set to other than C2_TIME_NEVER, and occurs in
   the past.
   Note that this differs from them buffer timeout error code of -ETIMEDOUT.
   @note Receiving a successful buffer completion callback is not a guarantee
   that a data transfer actually took place, but merely an indication that the
   transport reported the operation was successfully executed.  See the
   transport documentation for details.
 */
int c2_net_buffer_add(struct c2_net_buffer *buf,
		      struct c2_net_transfer_mc *tm);

/**
   Removes a registered buffer from a logical queue, if possible,
   cancelling any operation in progress.

   <b>Cancellation support is provided by the underlying transport.</b> It is
   not guaranteed that actual cancellation of the operation in progress will
   always be supported, and even if it is, there are race conditions in the
   execution of this request and the concurrent invocation of the completion
   callback on the buffer.

   The transport should set the C2_NET_BUF_CANCELLED flag in the buffer if
   the operation has not yet started.  The flag will be cleared by
   c2_net_buffer_event_post().

   The buffer completion callback is guaranteed to be invoked on a
   different thread.

   @pre (buf->nb_flags & C2_NET_BUF_REGISTERED)
   @param buf Specify the buffer pointer.
   @param tm  Specify the transfer machine pointer.
 */
void c2_net_buffer_del(struct c2_net_buffer *buf,
		       struct c2_net_transfer_mc *tm);

/**
   A transfer machine is notified of buffer related events with this
   subroutine.
   Typically, the subroutine is invoked by the transport associated with
   the transfer machine.

   The event data structure is not referenced from
   elsewhere after this subroutine returns, so may be allocated on the
   stack of the calling thread.

   Multiple concurrent events may be delivered for a given buffer, depending
   upon the transport.

   The subroutine will remove a buffer from its queue if the
   C2_NET_BUF_RETAIN flag is @em not set.  It will clear the C2_NET_BUF_QUEUED
   and C2_NET_BUF_IN_USE flags and set the nb_timeout field to C2_TIME_NEVER if
   the buffer is dequeued.  It will always clear the C2_NET_BUF_RETAIN,
   C2_NET_BUF_CANCELLED and C2_NET_BUF_TIMED_OUT flags prior to invoking the
   callback. The C2_NET_BUF_RETAIN flag must not be set if the status indicates
   error.

   If the C2_NET_BUF_CANCELLED flag was set, then the status must be
   -ECANCELED.

   If the C2_NET_BUF_TIMED_OUT flag was set, then the status must be
   -ETIMEDOUT.

   The subroutine will perform a c2_end_point_put() on the nbe_ep field
   in the event structure, if the queue type is C2_NET_QT_MSG_RECV and
   the nbe_status value is 0, and for the C2_NET_QT_MSG_SEND queue to
   match the c2_end_point_get() made in the c2_net_buffer_add() call.
   Care should be taken by the transport to accomodate these adjustments
   when invoking the subroutine with the C2_NET_BUF_RETAIN flag set.

   The subroutine will also signal to all waiters on the
   c2_net_transfer_mc.ntm_chan field after delivery of the callback.

   The invoking process should be aware that the callback subroutine could
   end up making re-entrant calls to the transport layer.

   @param ev Event pointer. The nbe_buffer field identifies the buffer,
   and the buffer's nb_tm field identifies the associated transfer machine.

   @see c2_net_tm_event_post()
 */
void c2_net_buffer_event_post(const struct c2_net_buffer_event *ev);

/**
   Deliver all pending network buffer events.  Should be called periodically
   by the application if synchronous network buffer event processing is
   enabled.
   @param tm Pointer to a transfer machine which has been set up for
   synchronous network buffer event processing.
   @see c2_net_buffer_event_deliver_synchronously(),
   c2_net_buffer_event_pending(), c2_net_buffer_event_notify()
   @pre tm->ntm_bev_auto_deliver is not set.
 */
void c2_net_buffer_event_deliver_all(struct c2_net_transfer_mc *tm);

/**
   This subroutine disables the automatic delivery of network buffer events.
   Instead, the application should use the c2_net_buffer_event_pending()
   subroutine to check for the presence of events, and the
   c2_net_buffer_event_deliver_all() subroutine to cause pending events to
   be delivered.  The c2_net_buffer_event_notify() subroutine can be used
   to get notified on a wait channel when buffer events arrive.

   Support for this mode of operation is transport specific.

   The subroutine must be invoked before the transfer machine is started.

   @param tm Pointer to an initialized but not started transfer machine.
   @pre tm->ntm_bev_auto_deliver is set.
   @post tm->ntm_bev_auto_deliver is not set.
   @see c2_net_buffer_event_pending(), c2_net_buffer_event_deliver_all(),
   c2_net_buffer_event_notify()
 */
int c2_net_buffer_event_deliver_synchronously(struct c2_net_transfer_mc *tm);

/**
   This subroutine determines if there are pending network buffer events that
   can be delivered with the c2_net_buffer_event_deliver_all() subroutine.
   @param tm Pointer to a transfer machine which has been set up for
   synchronous network buffer event processing.
   @see c2_net_buffer_event_deliver_synchronously()
   @pre tm->ntm_bev_auto_deliver is not set.
 */
bool c2_net_buffer_event_pending(struct c2_net_transfer_mc *tm);

/**
   This subroutine arranges for notification of the arrival of the next network
   buffer event to be signalled on the specified channel.  Typically, this
   subroutine is called only when the the c2_net_buffer_event_pending()
   subroutine indicates that there are no events pending.
   The subroutine does not block the invoker.
   @note The subroutine exhibits "monoshot" behavior - it only signals once
   on the specified wait channel.
   @param tm Pointer to a transfer machine which has been set up for
   synchronous network buffer event processing.
   @param chan The wait channel on which to send the signal.
   @see c2_net_buffer_event_deliver_synchronously()
   @pre tm->ntm_bev_auto_deliver is not set.
 */
void c2_net_buffer_event_notify(struct c2_net_transfer_mc *tm,
				struct c2_chan *chan);

/**
   Copies a network buffer descriptor.
   @param from_desc Specifies the source descriptor data structure.
   @param to_desc Specifies the destination descriptor data structure.
   @retval 0 (success)
   @retval -errno (failure)
 */
int c2_net_desc_copy(const struct c2_net_buf_desc *from_desc,
		     struct c2_net_buf_desc *to_desc);

/**
   Frees a network buffer descriptor.
   @param desc Specify the network buffer descriptor. Its fields will be
   cleared after this operation.
 */
void c2_net_desc_free(struct c2_net_buf_desc *desc);

enum {
	/* Hex ASCII value of "nb_lru" */
	C2_NET_BUFFER_LINK_MAGIC	 = 0x6e625f6c7275,
	/* Hex ASCII value of "nb_head" */
	C2_NET_BUFFER_HEAD_MAGIC	 = 0x6e625f68656164,
};

/** Descriptor for the tlist of buffers. */
C2_TL_DESCR_DECLARE(c2_net_pool, extern);
C2_TL_DESCR_DECLARE(c2_net_tm, extern);
C2_TL_DECLARE(c2_net_pool, extern, struct c2_net_buffer);
C2_TL_DECLARE(c2_net_tm, extern, struct c2_net_buffer);

/** @} */ /* end of networking group */

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
