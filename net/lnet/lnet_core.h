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
#ifndef __COLIBRI_LNET_CORE_H__
#define __COLIBRI_LNET_CORE_H__

/**
   @page LNetCoreDLD-fspec LNet Transport Core Functional Specfication
   <i>Mandatory. This page describes the external interfaces of the
   component. The section has mandatory sub-divisions created using the Doxygen
   @@section command.  It is required that there be Table of Contents at the
   top of the page that illustrates the sectioning of the page.</i>

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

   @see @ref KLNetCoreDLD "LNet Transport Kernel Core DLD"
   @see @ref ULNetCoreDLD "LNet Transport User Space Core DLD"

   @section LNetCoreDLD-fspec-ds API Data Structures
   The API requires that the transport application maintain API defined shared
   data for various network related objects:
   - c2_lnet_core_domain
   - c2_lnet_core_transfer_mc
   - c2_lnet_core_buffer

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
     - c2_lnet_core_buf_deregister()
     - c2_lnet_core_buf_register()
     - c2_lnet_core_dom_fini()
     - c2_lnet_core_dom_init()
     - c2_lnet_core_get_max_buffer_segment_size()
     - c2_lnet_core_get_max_buffer_size()
     - c2_lnet_core_tm_start()
     - c2_lnet_core_tm_stop()
     .
     These interfaces have names roughly similar to the associated
     c2_net_xprt_ops method from which they are intended to be directly or
     indirectly invoked.  Note that there are no equivalents for the @c
     xo_tm_init(), @c xo_tm_fini() and @c xo_tm_confine() calls.

   - End point address parsing subroutines:
     - c2_lnet_core_ep_addr_decode()
     - c2_lnet_core_ep_addr_encode()

   - Buffer operation related subroutines:
     - c2_lnet_core_buf_active_recv()
     - c2_lnet_core_buf_active_send()
     - c2_lnet_core_buf_del()
     - c2_lnet_core_buf_msg_recv()
     - c2_lnet_core_buf_msg_send()
     - c2_lnet_core_buf_passive_recv()
     - c2_lnet_core_buf_passive_send()
     - c2_lnet_core_buf_set_match_bits()
     .
     The buffer operation initiation calls are all invoked in the context of
     the c2_net_buffer_add() subroutine.  All operations are immediately
     initiated in the Lustre LNet kernel module, though results will be
     returned asynchronously through buffer events.

   - Event processing calls:
     - c2_lnet_core_buf_event_wait()
     - c2_lnet_core_buf_event_get()
     .
     The Core API offers no callback mechanism.  Instead, the transport must
     poll for events.  Typically this is done on one or more dedicated threads,
     which exhibit the desired processor affiliation required by the higher
     software layers.

 */

/**
   @defgroup LNetCore LNet Transport Core Interface
   @ingroup LNetDFS

   The internal, address space agnostic I/O API used by the LNet transport.

   @see @ref LNetCoreDLD-fspec "LNet Transport Core Functional Specification"

   @{
 */

#include "net/lnet.h"

/* forward references */
struct c2_lnet_core_bev_link;
struct c2_lnet_core_bev_cqueue;
struct c2_lnet_core_buffer;
struct c2_lnet_core_buffer_event;
struct c2_lnet_core_domain;
struct c2_lnet_core_ep_addr;
struct c2_lnet_core_transfer_mc;

/**
   Opaque type wide enough to represent an address in any address space.
 */
typedef uint64_t c2_lnet_core_opaque_ptr_t;
C2_BASSERT(sizeof(c2_lnet_core_opaque_ptr_t) == sizeof(void *));

/**
   This structure defines the fields in an LNet transport end point address.
 */
struct c2_lnet_core_ep_addr {
	uint64_t lcepa_nid;   /**< The LNet Network Identifier */
	uint32_t lcepa_pid;   /**< The LNet Process Identifier */
	uint32_t lcepa_portal;/**< The LNet Portal Number */
	uint32_t lcepa_tmid;  /**< The Transfer Machine Identifier */
};

enum {
	/** Number of bits used for TM identifier */
	C2_NET_LNET_TMID_NUM_BITS = 12,
	/** Max TM identifier is 2^^12-1 */
	C2_NET_LNET_TMID_MAX      = 4095,
	/** Invalid value used for dynamic addressing */
	C2_NET_LNET_TMID_INVALID  = C2_NET_LNET_TMID_MAX+1,
	/** Minimum buffer match bit counter value */
	C2_NET_LNET_MATCH_BIT_MIN = 1,
	/** Maximum buffer match bit counter value: 2^^52-1; */
	C2_NET_LNET_MATCH_BIT_MAX = 0xfffffffffffffULL,
};

/**
   Buffer events are linked in the buffer queue using this structure. It is
   designed to be operated upon from either kernel or user space with a single
   producer and single consumer.
 */
struct c2_lnet_core_bev_link {
	/**
	   Self pointer in the transport address space.
	 */
	c2_lnet_core_opaque_ptr_t lcbevl_t_self;

	/**
	   Self pointer in the kernel address space.
	 */
	c2_lnet_core_opaque_ptr_t lcbevl_k_self;

	/**
	   Pointer to the next element in the consumer address space.
	 */
	c2_lnet_core_opaque_ptr_t lcbevl_c_next;
};

/**
   Buffer event queue, operable from either from either kernel and user space
   with a single producer and single consumer.
 */
struct c2_lnet_core_bev_cqueue {
	/**
	   Number of elements currently in the queue.
	 */
	size_t                   lcbevq_nr;

	/**
	   The producer adds links to this anchor.
	   The producer pointer value is in the address space of the
	   producer.
	 */
	c2_lnet_core_opaque_ptr_t lcbevq_producer;

	/**
	   The consumer removes elements from this anchor.
	   The producer pointer value is in the address space of the
	   producer.
	 */
	c2_lnet_core_opaque_ptr_t lcbevq_consumer;
};

/**
   This structure describes a buffer event. It is very similar to
   struct c2_net_buffer_event.
 */
struct c2_lnet_core_buffer_event {
	/** Linkage in one of the TM buffer event queues */
	struct c2_lnet_core_bev_link lcbe_tm_link;

	/**
	    This value is set by the kernel Core module's LNet event handler,
	    and is copied from the c2_lnet_core_buffer::lcb_buffer_id
	    field. The value is a pointer to the core buffer data in the
	    transport address space, and is provided to enable the transport
	    to navigate back to its outer buffer private data and from there
	    back to the c2_net_buffer.
	 */
	c2_lnet_core_opaque_ptr_t    lcbe_core_buf;

	/** Event timestamp */
	c2_time_t                    lcbe_time;

	/** Status code (-errno). 0 is success */
	int32_t                      lcbe_status;

	/** Length of data in the buffer */
	c2_bcount_t                  lcbe_length;

	/** Offset of start of the data in the buffer. (Receive only) */
	c2_bcount_t                  lcbe_offset;

	/** Address of the other end point */
	struct c2_lnet_core_ep_addr  lcbe_sender;

	/** True if the buffer is no longer in use */
        bool                         lcbe_unlinked;
};

/**
   Core domain data.  The transport layer should embed this in its private data.
*/
struct c2_lnet_core_domain {
	/* place holder */
};


/**
   Core transfer machine data.  The transport layer should embed this in its
   private data.
*/
struct c2_lnet_core_transfer_mc {
	/** The transfer machine address */
	struct c2_lnet_ep_addr  lctm_addr;

	/** Boolean indicating if the transport is running in user space. */
	bool                    lctm_user_space_xo;

	void                   *lctm_upvt; /**< Core user space private */
	void                   *lctm_kpvt; /**< Core kernel space private */

	/**
	   List of available buffer event structures.  The queue is shared
	   between the transport address space and the kernel.

	   The transport is responsible for ensuring that there are sufficient
	   free entries to return the results of all pending operations.
	 */
	struct c2_lnet_core_bev_cqueue lctm_free_bevq;

	/**
	   Buffer completion event queue.  The queue is shared between the
	   transport address space and the kernel.
	 */
	struct c2_lnet_core_bev_cqueue lctm_bevq;
};

/**
   Core buffer data.  The transport layer should embed this in its private data.
*/
struct c2_lnet_core_buffer {
	/**
	   The address of the c2_net_buffer structure in the transport address
	   space. The value is set by the c2_lnet_core_buffer_register()
	   subroutine.
	 */
	c2_lnet_core_opaque_ptr_t lcb_buffer_id;

	/**
	   The buffer queue type - copied from c2_net_buffer::nb_qtype
	   when the buffer operation is initiated.
	 */
        enum c2_net_queue_type    lcb_qtype;

	/**
	   The match bits for a passive bulk buffer, including the TMID field.
	   They should be set using the c2_lnet_core_tm_match_bits_set()
	   subroutine.

	   The file is also used in an active buffer to describe the match
	   bits of the remote passive buffer.
	 */
	uint64_t              lcb_match_bits;

	/**
	   The address of the destination transfer machine is set in this field
	   for buffers on the C2_NET_QT_MSG_SEND queue.

	   The address of the remote passive transfer machine is set in this
	   field for buffers on the C2_NET_QT_ACTIVE_BULK_SEND or
	   C2_NET_QT_ACTIVE_BULK_RECV queues.
	 */
	struct c2_lnet_core_ep_addr lcb_addr;

	void                 *lcb_upvt; /**< Core user space private */
	void                 *lcb_kpvt; /**< Core kernel space private */
};


/**
   Allocate and initialize the network domain's private field for use by LNet.
   @param dom The network domain pointer.
   @param lcom The private data pointer for the domain to be initialized.
 */
static int c2_lnet_core_dom_init(struct c2_net_domain *dom,
				 struct c2_lnet_core_domain *lcdom);

/**
   Release LNet transport resources related to the domain.
 */
static int c2_lnet_core_dom_fini(struct c2_lnet_core_domain *lcdom);

/**
   Get the maximum buffer size (counting all segments).
 */
static c2_bcount_t c2_lnet_core_get_max_buffer_size(struct c2_lnet_core_domain
						    *lcdom);

/**
   Get the maximum size of a buffer segment.
 */
static c2_bcount_t c2_lnet_core_get_max_buffer_segment_size(
                                             struct c2_lnet_core_domain *lcdom);

/**
   Get the maximum number of buffer segments.
 */
static int32_t c2_lnet_core_get_max_buffer_segments(
                                             struct c2_lnet_core_domain *lcdom);

/**
   Register a network buffer.  In user space this results in the buffer memory
   getting pinned.
   The subroutine allocates private data to associate with the network buffer.
   @param lcdom The domain private data to be initialized.
   @param buf The network buffer pointer with its nb_dom field set.
   @param lcbuf The core private data pointer for the buffer.
   @pre buf->nb_dom != NULL
 */
static int c2_lnet_core_buf_register(struct c2_lnet_core_domain *lcdom,
				     struct c2_net_buffer *buf,
				     struct c2_lnet_core_buffer *lcbuf);

/**
   Deregister the buffer.
   @param lcdom The domain private data to be initialized.
   @param The buffer private data.
 */
static int c2_lnet_core_buf_deregister(struct c2_lnet_core_domain *lcdom,
				       struct c2_lnet_core_buffer *lcbuf);

/**
   Enqueue a buffer for message reception. Multiple messages may be received
   into the buffer, space permitting, up to the configured maximum.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
 */
static int c2_lnet_core_buf_msg_recv(struct c2_lnet_core_transfer_mc *lctm,
				     struct c2_lnet_core_buffer *lcbuf);

/**
   Enqueue a buffer for message transmission.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre lcbuf->lcb_addr is valid
 */
static int c2_lnet_core_buf_msg_send(struct c2_lnet_core_transfer_mc *lctm,
				     struct c2_lnet_core_buffer *lcbuf);

/**
   Enqueue a buffer for active bulk receive.
   The lcb_match_bits field should be set to the value of the match bits of the
   remote passive buffer.
   The lcb_addr field should be set with the end point address of the
   transfer machine with the passive buffer.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
   @pre lcbuf->lcb_match_bits != 0
   @pre lcbuf->lcb_addr is valid
 */
static int c2_lnet_core_buf_active_recv(struct c2_lnet_core_transfer_mc *lctm,
					struct c2_lnet_core_buffer *lcbuf);

/**
   Enqueue a buffer for active bulk send.
   The lcb_match_bits field should be set to the value of the match bits of the
   remote passive buffer.
   The lcb_addr field should be set with the end point address of the
   transfer machine with the passive buffer.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
   @pre lcbuf->lcb_match_bits != 0
   @pre lcbuf->lcb_addr is valid
 */
static int c2_lnet_core_buf_active_send(struct c2_lnet_core_transfer_mc *lctm,
					struct c2_lnet_core_buffer *lcbuf);

/**
   This subroutine generates new match bits for the given buffer's
   lcb_match_bits field.

   It is intended to be used by the transport prior to invoking passive buffer
   operations.  The reason it is not combined with the passive operation
   subroutines is that the core API does not guarantee unique match bits.  The
   match bit counter will wrap over time, though, being a very large counter,
   it would take considerable time before it does wrap.

   @param lctm  Transfer machine private data.
   @param lcbuf The buffer private data.
   @pre The buffer is queued on the specified transfer machine.
 */
static void c2_lnet_core_buf_set_match_bits(struct c2_lnet_core_transfer_mc
					    *lctm,
					    struct c2_lnet_core_buffer *lcbuf);

/**
   Enqueue a buffer for passive bulk receive.
   The match bits for the passive buffer should be set in the buffer with the
   c2_lnet_core_buf_set_match_bits() subroutine before this call.
   It is guaranteed that the buffer can be remotely accessed when the
   subroutine returns.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
   @pre lcbuf->lcb_match_bits != 0
 */
static int c2_lnet_core_buf_passive_recv(struct c2_lnet_core_transfer_mc *lctm,
					 struct c2_lnet_core_buffer *lcbuf);

/**
   Enqueue buffer for passive bulk send.
   The match bits for the passive buffer should be set in the buffer with the
   c2_lnet_core_buf_set_match_bits() subroutine before this call.
   It is guaranteed that the buffer can be remotely accessed when the
   subroutine returns.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
   @pre lcbuf->lcb_match_bits != 0
 */
static int c2_lnet_core_buf_passive_send(struct c2_lnet_core_transfer_mc *lctm,
					 struct c2_lnet_core_buffer *lcbuf);

/**
   Cancel a buffer operation if possible.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
 */
static int c2_lnet_core_buf_del(struct c2_lnet_core_transfer_mc *lctm,
				struct c2_lnet_core_buffer *lcbuf);

/**
   Subroutine to wait for buffer events, or the timeout.
   @param lctm Transfer machine private data.
   @param timeout Absolute time at which to stop waiting.
   @retval 0 Events present.
   @retval -ETIMEDOUT Timed out.
 */
static int c2_lnet_core_buf_event_wait(struct c2_lnet_core_transfer_mc *lctm,
				       c2_time_t timeout);

/**
   Fetch the next event from the circular buffer event queue.  This subroutine
   does not support concurrent invocation; the transport should serialize
   access if more than one thread is used to process events.
   @param lctm Transfer machine private data.
   @param lcbe Returns the next buffer event.
   @retval true Event returned.
   @retval false No events on the queue.
 */
static bool c2_lnet_core_buf_event_get(struct c2_lnet_core_transfer_mc *lctm,
				       struct c2_lnet_buffer_event *lcbe);

/**
   Subroutine to parse an end point address string and convert to internal form.
   A "*" value for the transfer machine identifier results in a value of
   C2_NET_LNET_TMID_INVALID being set.
 */
static int c2_lnet_core_ep_addr_decode(struct c2_lnet_core_domain *lcdom,
				       const char *ep_addr,
				       struct c2_lnet_core_ep_addr *lcepa);

/**
   Subroutine to construct the external address string from its internal form.
   A value of C2_NET_LNET_TMID_INVALID for the lcepa_tmid field results in
   a "*" being set for that field.
 */
static void c2_lnet_core_ep_addr_encode(struct c2_lnet_core_domain *lcdom,
					struct c2_lnet_core_ep_addr *lcepa,
					char buf[C2_NET_LNET_XEP_ADDR_LEN]);

/**
   Subroutine to start a transfer machine. Internally this results in
   the creation of the LNet EQ associated with the transfer machine.
   @param tm The transfer machine pointer.
   @param lctm The transfer machine private data to be initialized.
   @param lcepa The end point address of this transfer machine. If the
   lcpea_tmid field value is C2_NET_LNET_TMID_INVALID then a transfer machine
   identifier is dynamically assigned to the transfer machine and returned
   in this structure itself.
   @note There is no equivalent of the xo_tm_init() subroutine.
 */
static int c2_lnet_core_tm_start(struct c2_net_transfer_mc *tm,
				 struct c2_lnet_core_transfer_mc *lctm,
				 struct c2_lnet_core_ep_addr *lcepa);

/**
   Stop the transfer machine and release associated resources.  All operations
   must be finalized prior to this call.
   @param lctm The transfer machine private data.
   @note There is no equivalent of the xo_tm_fini() subroutine.
 */
static int c2_lnet_core_tm_stop(struct c2_lnet_core_transfer_mc *lctm);

/**
   @}
*/

#endif /* __COLIBRI_LNET_CORE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
