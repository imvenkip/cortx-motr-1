/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 11/01/2011
 *
 */
#ifndef __COLIBRI_NET_LNET_CORE_H__
#define __COLIBRI_NET_LNET_CORE_H__

/**
   @page LNetCoreDLD-fspec LNet Transport Core Functional Specfication

   - @ref LNetCoreDLD-fspec-ovw
   - @ref LNetCoreDLD-fspec-ds
   - @ref LNetCoreDLD-fspec-subs
   - @ref LNetCore "LNet Transport Core Interface"

   @section LNetCoreDLD-fspec-ovw API Overview
   The LNet Transport Core presents an address space agnostic API to the LNet
   Transport layer.  These interfaces are declared in the file
   @ref net/lnet/lnet_core.h.

   The interface is implemented differently in the kernel and in user space.
   The kernel interface interacts directly with LNet; the user space interface
   uses a device driver to communicate with its kernel counterpart and
   uses shared memory to avoid event data copy.

   The Core API offers no callback mechanism.  Instead, the transport must
   poll for events.  Typically this is done on one or more dedicated threads,
   which exhibit the desired processor affiliation required by the higher
   software layers.

   The following sequence diagram illustrates the typical operational flow:
   @msc
   A [label="Application"],
   N [label="Network"],
   x [label="XO Method"],
   t [label="XO Event\nThread"],
   o [label="Core\nOps"],
   e [label="Core\nEvent Queue"],
   L [label="LNet\nOps"],
   l [label="LNet\nCallback"];

   t=>e  [label="Wait"];
   ...;
   A=>N  [label="c2_net_buffer_add()"];
   N=>x  [label="xo_buf_add()"];
   x=>o  [label="nlx_core_buf_op()"];
   o=>L  [label="MD Operation"];
   L>>o;
   o>>x;
   x>>N;
   N>>A;
   ...;
   l=>>e [label="EQ callback"];
   e>>t  [label="Events present"];
   t=>e  [label="Get Event"];
   e>>t  [label="event"];
   N<<=t [label="c2_net_buffer_event_post()"];
   N=>>A [label="callback"];
   t=>e  [label="Get Event"];
   e>>t  [label="empty"];
   t=>e  [label="Wait"];
   ...;
   @endmsc

   @section LNetCoreDLD-fspec-ds API Data Structures
   The API requires that the transport application maintain API defined shared
   data for various network related objects:
   - nlx_core_domain
   - nlx_core_transfer_mc
   - nlx_core_buffer

   The sharing takes place between the transport layer and the core layer.
   This will span the kernel and user address space boundary when using the
   user space transport.

   These shared data structures should be embedded in the transport
   application's own private data.  This requirement results in an
   initialization call pattern that takes a pointer to the standard network
   layer data structure concerned and a pointer to the API's data structure.

   Subsequent calls to the API only pass the API data structure pointer.  The
   API data structure must be eventually finalized.

   @section LNetCoreDLD-fspec-subs Subroutines
   The API subroutines are categorized as follows:

   - Initialization, finalization, cancellation and query subroutines:
     - nlx_core_buf_deregister()
     - nlx_core_buf_register()
     - nlx_core_dom_fini()
     - nlx_core_dom_init()
     - nlx_core_get_max_buffer_segment_size()
     - nlx_core_get_max_buffer_size()
     - nlx_core_tm_start()
     - nlx_core_tm_stop()
     .
     These interfaces have names roughly similar to the associated
     c2_net_xprt_ops method from which they are intended to be directly or
     indirectly invoked.  Note that there are no equivalents for the @c
     xo_tm_init(), @c xo_tm_fini() and @c xo_tm_confine() calls.

   - End point address parsing subroutines:
     - nlx_core_ep_addr_decode()
     - nlx_core_ep_addr_encode()

   - Buffer operation related subroutines:
     - nlx_core_buf_active_recv()
     - nlx_core_buf_active_send()
     - nlx_core_buf_del()
     - nlx_core_buf_msg_recv()
     - nlx_core_buf_msg_send()
     - nlx_core_buf_passive_recv()
     - nlx_core_buf_passive_send()
     - nlx_core_buf_match_bits_set()
     .
     The buffer operation initiation calls are all invoked in the context of
     the c2_net_buffer_add() subroutine.  All operations are immediately
     initiated in the Lustre LNet kernel module, though results will be
     returned asynchronously through buffer events.

   - Event processing calls:
     - nlx_core_buf_event_wait()
     - nlx_core_buf_event_get()
     .
     The API assumes that only a single transport thread will be used to
     process events.

   Invocation of the buffer operation initiation subroutines and the
   nlx_core_buf_event_get() subroutine should be serialized.

   @see @ref KLNetCoreDLD "LNet Transport Kernel Core DLD"
   @see @ref ULNetCoreDLD "LNet Transport User Space Core DLD"

 */

/**
   @defgroup LNetCore LNet Transport Core Interface
   @ingroup LNetDFS

   The internal, address space agnostic I/O API used by the LNet transport.

   @see @ref LNetCoreDLD-fspec "LNet Transport Core Functional Specification"

   @{
 */

#include "net/lnet/lnet.h"

/* forward references */
struct nlx_core_bev_link;
struct nlx_core_bev_cqueue;
struct nlx_core_buffer;
struct nlx_core_buffer_event;
struct nlx_core_domain;
struct nlx_core_ep_addr;
struct nlx_core_transfer_mc;

/**
   Opaque type wide enough to represent an address in any address space.
 */
typedef uint64_t nlx_core_opaque_ptr_t;
C2_BASSERT(sizeof(nlx_core_opaque_ptr_t) >= sizeof(void *));

/**
   This structure defines the fields in an LNet transport end point address.
 */
struct nlx_core_ep_addr {
	uint64_t cepa_nid;   /**< The LNet Network Identifier */
	uint32_t cepa_pid;   /**< The LNet Process Identifier */
	uint32_t cepa_portal;/**< The LNet Portal Number */
	uint32_t cepa_tmid;  /**< The Transfer Machine Identifier */
};

/* Match bit related definitions */
enum {
	/** Number of bits used for TM identifier */
	C2_NET_LNET_TMID_BITS      = 12,
	/** Shift to the TMID position (52) */
	C2_NET_LNET_TMID_SHIFT     = 64 - C2_NET_LNET_TMID_BITS,
	/** Max TM identifier is 2^12-1 (4095) */
	C2_NET_LNET_TMID_MAX       = (1 << C2_NET_LNET_TMID_BITS) - 1,
	/** Invalid value used for dynamic addressing */
	C2_NET_LNET_TMID_INVALID   = C2_NET_LNET_TMID_MAX+1,
	/** Number of bits used for buffer identification (52) */
	C2_NET_LNET_BUFFER_ID_BITS = 64 - C2_NET_LNET_TMID_BITS,
	/** Minimum buffer match bit counter value */
	C2_NET_LNET_BUFFER_ID_MIN  = 1,
	/** Maximum buffer match bit counter value: 2^52-1 (0xfffffffffffff) */
	C2_NET_LNET_BUFFER_ID_MAX  = (1ULL << C2_NET_LNET_BUFFER_ID_BITS) - 1,
};
C2_BASSERT(C2_NET_LNET_TMID_BITS + C2_NET_LNET_BUFFER_ID_BITS <= 64);

/**
   Buffer events are linked in the buffer queue using this structure. It is
   designed to be operated upon from either kernel or user space with a single
   producer and single consumer.
 */
struct nlx_core_bev_link {
	/**
	   Self pointer in the consumer (transport) address space.
	 */
	nlx_core_opaque_ptr_t cbl_c_self;

	/**
	   Self pointer in the producer (kernel) address space.
	 */
	nlx_core_opaque_ptr_t cbl_p_self;

	/**
	   Pointer to the next element in the consumer address space.
	 */
	nlx_core_opaque_ptr_t cbl_c_next;

	/**
	   Pointer to the next element in the producer address space.
	 */
	nlx_core_opaque_ptr_t cbl_p_next;
};

/**
   Buffer event queue, operable from either kernel and user space
   with a single producer and single consumer.
 */
struct nlx_core_bev_cqueue {
	/**
	   Number of elements currently in the queue.
	 */
	size_t                 cbcq_nr;

	/**
	   The producer adds links to this anchor.
	   The producer pointer value is in the address space of the
	   producer (kernel).
	 */
	nlx_core_opaque_ptr_t cbcq_producer;

	/**
	   The consumer removes elements from this anchor.
	   The consumer pointer value is in the address space of the
	   consumer (transport).
	 */
	nlx_core_opaque_ptr_t cbcq_consumer;
};

/**
   This structure describes a buffer event. It is very similar to
   struct c2_net_buffer_event.
 */
struct nlx_core_buffer_event {
	/** Linkage in one of the TM buffer event queues */
	struct nlx_core_bev_link     cbe_tm_link;

	/**
	    This value is set by the kernel Core module's LNet event handler,
	    and is copied from the nlx_core_buffer::cb_buffer_id
	    field. The value is a pointer to the core buffer data in the
	    transport address space, and is provided to enable the transport
	    to navigate back to its outer buffer private data and from there
	    back to the c2_net_buffer.
	 */
	nlx_core_opaque_ptr_t        cbe_core_buf;

	/** Event timestamp */
	c2_time_t                    cbe_time;

	/** Status code (-errno). 0 is success */
	int32_t                      cbe_status;

	/** Length of data in the buffer */
	c2_bcount_t                  cbe_length;

	/** Offset of start of the data in the buffer. (Receive only) */
	c2_bcount_t                  cbe_offset;

	/** Address of the other end point */
	struct nlx_core_ep_addr      cbe_sender;

	/** True if the buffer is no longer in use */
        bool                         cbe_unlinked;
};

/**
   Core domain data.  The transport layer should embed this in its private data.
*/
struct nlx_core_domain {

	void *cd_upvt; /**< Core user space private */
	void *cd_kpvt; /**< Core kernel space private */

};

/**
   Core transfer machine data.  The transport layer should embed this in its
   private data.
*/
struct nlx_core_transfer_mc {
	/** The transfer machine address */
	struct c2_lnet_ep_addr     ctm_addr;

	/** Boolean indicating if the transport is running in user space. */
	bool                       ctm_user_space_xo;

	/**
	   List of available buffer event structures.  The queue is shared
	   between the transport address space and the kernel.

	   The transport is responsible for ensuring that there are sufficient
	   free entries to return the results of all pending operations.
	 */
	struct nlx_core_bev_cqueue ctm_free_bevq;

	/**
	   Buffer completion event queue.  The queue is shared between the
	   transport address space and the kernel.
	 */
	struct nlx_core_bev_cqueue ctm_bevq;

	void                      *ctm_upvt; /**< Core user space private */
	void                      *ctm_kpvt; /**< Core kernel space private */
};

/**
   Core buffer data.  The transport layer should embed this in its private data.
*/
struct nlx_core_buffer {
	/**
	   The address of the c2_net_buffer structure in the transport address
	   space. The value is set by the nlx_core_buffer_register()
	   subroutine.
	 */
	nlx_core_opaque_ptr_t   cb_buffer_id;

	/**
	   The buffer queue type - copied from c2_net_buffer::nb_qtype
	   when the buffer operation is initiated.
	 */
        enum c2_net_queue_type  cb_qtype;

	/**
	   The match bits for a passive bulk buffer, including the TMID field.
	   They should be set using the nlx_core_buf_match_bits_set()
	   subroutine.

	   The file is also used in an active buffer to describe the match
	   bits of the remote passive buffer.
	 */
	uint64_t                cb_match_bits;

	/**
	   The address of the destination transfer machine is set in this field
	   for buffers on the C2_NET_QT_MSG_SEND queue.

	   The address of the remote passive transfer machine is set in this
	   field for buffers on the C2_NET_QT_ACTIVE_BULK_SEND or
	   C2_NET_QT_ACTIVE_BULK_RECV queues.
	 */
	struct nlx_core_ep_addr cb_addr;

	void                   *cb_upvt; /**< Core user space private */
	void                   *cb_kpvt; /**< Core kernel space private */
};


/**
   Allocates and initializes the network domain's private field for use by LNet.
   @param dom The network domain pointer.
   @param lcdom The private data pointer for the domain to be initialized.
 */
static int nlx_core_dom_init(struct c2_net_domain *dom,
			     struct nlx_core_domain *lcdom);

/**
   Releases LNet transport resources related to the domain.
 */
static int nlx_core_dom_fini(struct nlx_core_domain *lcdom);

/**
   Gets the maximum buffer size (counting all segments).
 */
static c2_bcount_t nlx_core_get_max_buffer_size(struct nlx_core_domain *lcdom);

/**
   Gets the maximum size of a buffer segment.
 */
static c2_bcount_t nlx_core_get_max_buffer_segment_size(struct
							nlx_core_domain *lcdom);

/**
   Gets the maximum number of buffer segments.
 */
static int32_t nlx_core_get_max_buffer_segments(struct nlx_core_domain *lcdom);

/**
   Registers a network buffer.  In user space this results in the buffer memory
   getting pinned.
   The subroutine allocates private data to associate with the network buffer.
   @param lcdom The domain private data to be initialized.
   @param buf The network buffer pointer with its nb_dom field set.
   @param lcbuf The core private data pointer for the buffer.
   @pre buf->nb_dom != NULL
 */
static int nlx_core_buf_register(struct nlx_core_domain *lcdom,
				 struct c2_net_buffer *buf,
				 struct nlx_core_buffer *lcbuf);

/**
   Deregisters the buffer.
   @param lcdom The domain private data to be initialized.
   @param lcbuf The buffer private data.
 */
static int nlx_core_buf_deregister(struct nlx_core_domain *lcdom,
				   struct nlx_core_buffer *lcbuf);

/**
   Enqueues a buffer for message reception. Multiple messages may be received
   into the buffer, space permitting, up to the configured maximum.

   The invoker should ensure that the subroutine is not invoked concurrently
   with any of the other buffer operation initiation subroutines or the
   nlx_core_buf_event_get() subroutine.

   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
 */
static int nlx_core_buf_msg_recv(struct nlx_core_transfer_mc *lctm,
				 struct nlx_core_buffer *lcbuf);

/**
   Enqueues a buffer for message transmission.

   The invoker should ensure that the subroutine is not invoked concurrently
   with any of the other buffer operation initiation subroutines or the
   nlx_core_buf_event_get() subroutine.

   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre lcbuf->cb_addr is valid
 */
static int nlx_core_buf_msg_send(struct nlx_core_transfer_mc *lctm,
				 struct nlx_core_buffer *lcbuf);

/**
   Enqueues a buffer for active bulk receive.
   The cb_match_bits field should be set to the value of the match bits of the
   remote passive buffer.
   The cb_addr field should be set with the end point address of the
   transfer machine with the passive buffer.

   The invoker should ensure that the subroutine is not invoked concurrently
   with any of the other buffer operation initiation subroutines or the
   nlx_core_buf_event_get() subroutine.

   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
   @pre lcbuf->cb_match_bits != 0
   @pre lcbuf->cb_addr is valid
 */
static int nlx_core_buf_active_recv(struct nlx_core_transfer_mc *lctm,
				    struct nlx_core_buffer *lcbuf);

/**
   Enqueues a buffer for active bulk send.
   See nlx_core_buf_active_recv() for how the buffer is to be initialized.

   The invoker should ensure that the subroutine is not invoked concurrently
   with any of the other buffer operation initiation subroutines or the
   nlx_core_buf_event_get() subroutine.

   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
   @pre lcbuf->cb_match_bits != 0
   @pre lcbuf->cb_addr is valid
 */
static int nlx_core_buf_active_send(struct nlx_core_transfer_mc *lctm,
				    struct nlx_core_buffer *lcbuf);

/**
   This subroutine generates new match bits for the given buffer's
   cb_match_bits field.

   It is intended to be used by the transport prior to invoking passive buffer
   operations.  The reason it is not combined with the passive operation
   subroutines is that the core API does not guarantee unique match bits.  The
   match bit counter will wrap over time, though, being a very large counter,
   it would take considerable time before it does wrap.

   @param lctm  Transfer machine private data.
   @param lcbuf The buffer private data.
   @pre The buffer is queued on the specified transfer machine.
 */
static void nlx_core_buf_match_bits_set(struct nlx_core_transfer_mc *lctm,
					struct nlx_core_buffer *lcbuf);

/**
   Enqueues a buffer for passive bulk receive.
   The match bits for the passive buffer should be set in the buffer with the
   nlx_core_buf_match_bits_set() subroutine before this call.
   It is guaranteed that the buffer can be remotely accessed when the
   subroutine returns.

   The invoker should ensure that the subroutine is not invoked concurrently
   with any of the other buffer operation initiation subroutines or the
   nlx_core_buf_event_get() subroutine.

   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
   @pre lcbuf->cb_match_bits != 0
 */
static int nlx_core_buf_passive_recv(struct nlx_core_transfer_mc *lctm,
				     struct nlx_core_buffer *lcbuf);

/**
   Enqueues a buffer for passive bulk send.
   See nlx_core_buf_passive_recv() for how the buffer is to be initialized.
   It is guaranteed that the buffer can be remotely accessed when the
   subroutine returns.

   The invoker should ensure that the subroutine is not invoked concurrently
   with any of the other buffer operation initiation subroutines or the
   nlx_core_buf_event_get() subroutine.

   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
   @pre lcbuf->cb_match_bits != 0
 */
static int nlx_core_buf_passive_send(struct nlx_core_transfer_mc *lctm,
				     struct nlx_core_buffer *lcbuf);

/**
   Cancels a buffer operation if possible.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
 */
static int nlx_core_buf_del(struct nlx_core_transfer_mc *lctm,
			    struct nlx_core_buffer *lcbuf);

/**
   Waits for buffer events, or the timeout.
   @param lctm Transfer machine private data.
   @param timeout Absolute time at which to stop waiting.  A value of 0
   indicates that the subroutine should not wait.
   @retval 0 Events present.
   @retval -ETIMEDOUT Timed out before events arrived.
 */
static int nlx_core_buf_event_wait(struct nlx_core_transfer_mc *lctm,
				   c2_time_t timeout);

/**
   Fetches the next event from the circular buffer event queue.

   The invoker should ensure that the subroutine is not invoked concurrently
   with any of the buffer operation initiation subroutines, or another
   invocation of itself.

   @param lctm Transfer machine private data.
   @param lcbe The next buffer event is returned here.
   @retval true Event returned.
   @retval false No events on the queue.
 */
static bool nlx_core_buf_event_get(struct nlx_core_transfer_mc *lctm,
				   struct nlx_core_buffer_event *lcbe);

/**
   Parses an end point address string and convert to internal form.
   A "*" value for the transfer machine identifier results in a value of
   C2_NET_LNET_TMID_INVALID being set.
 */
static int nlx_core_ep_addr_decode(struct nlx_core_domain *lcdom,
				   const char *ep_addr,
				   struct nlx_core_ep_addr *cepa);

/**
   Constructs the external address string from its internal form.
   A value of C2_NET_LNET_TMID_INVALID for the cepa_tmid field results in
   a "*" being set for that field.
 */
static void nlx_core_ep_addr_encode(struct nlx_core_domain *lcdom,
				    struct nlx_core_ep_addr *cepa,
				    char buf[C2_NET_LNET_XEP_ADDR_LEN]);

/**
   Starts a transfer machine. Internally this results in
   the creation of the LNet EQ associated with the transfer machine.
   @param tm The transfer machine pointer.
   @param lctm The transfer machine private data to be initialized.
   @param cepa The end point address of this transfer machine. If the
   lcpea_tmid field value is C2_NET_LNET_TMID_INVALID then a transfer machine
   identifier is dynamically assigned to the transfer machine and returned
   in this structure itself.
   @note There is no equivalent of the xo_tm_init() subroutine.
 */
static int nlx_core_tm_start(struct c2_net_transfer_mc *tm,
			     struct nlx_core_transfer_mc *lctm,
			     struct nlx_core_ep_addr *cepa);

/**
   Stops the transfer machine and release associated resources.  All operations
   must be finalized prior to this call.
   @param lctm The transfer machine private data.
   @note There is no equivalent of the xo_tm_fini() subroutine.
 */
static void nlx_core_tm_stop(struct nlx_core_transfer_mc *lctm);

/**
   @}
*/

#endif /* __COLIBRI_NET_LNET_CORE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
