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
   - @ref LNetCoreDLD-fspec-usage
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

   The sharing is defined as taking place between the transport layer and the
   core layer.  This will span the kernel and user address space boundary when
   using the user space transport.

   These data structures should be embedded in the transport application's own
   private data.  This requirement results in initialization calls that require
   a pointer to the standard network layer data structure concerned and a
   pointer to the API's data structure.  Note that these data structures are of
   variable length, and should be embedded at the end of the transport's
   private structure.  By implication, their presence makes the associated
   transport private data structures variable length.

   Subsequent calls to the API only pass the API data structure pointer.  The
   API data must be eventually finalized.

   @section LNetCoreDLD-fspec-usage API Usage
   The API is intended to be used in the following contexts:

   - Initialization, finalization and query calls: These are invoked from the
     methods of the c2_net_xprt_ops structure.  Most of the interfaces have
     names roughly similar to the associated c2_net_xprt_ops method from which
     they are intended to be directly or indirectly invoked.  One notable
     exception is that there are no equivalents for the @c xo_tm_init and @c
     xo_tm_fini calls.

   - Buffer operation initiation calls: Operations should be initiated by the
     transport only when there is sufficient buffer event queue space in which
     to return the result. Typically this would be done off a transport work
     queue.  See @ref KLNetCoreDLD-lspec-thread for further details.

   - Event processing calls: These are invoked on threads maintained by the
     transport.  Such threads usually have some processor affiliation required
     by the higher software layers.

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
struct c2_lnet_core_buffer;
struct c2_lnet_core_buffer_event;
struct c2_lnet_core_domain;
struct c2_lnet_core_ep_addr;
struct c2_lnet_core_transfer_mc;

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
	C2_NET_LNET_TMID_NUM_BITS = 12, /**< Number of bits used for TM id */
	C2_NET_LNET_TMID_INVALID = 4097, /**< Invalid value used for dynamic */
	/** Minium match bit value */
	C2_NET_LNET_MATCH_BIT_MIN = 1,
	/** Maximum match bit value: 2^^52; */
	C2_NET_LNET_MATCH_BIT_MAX = 0xffffffffffffULL,
};

/**
   Core domain data.  The transport layer should embed this at the
   <b>very end</b> of its private data.
 */
struct c2_lnet_core_domain {
	/* place holder */
};

/**
   Opaque type wide enough to represent an address in any address space.
 */
typedef uint64_t c2_lnet_core_opaque_ptr_t;
C2_BASSERT(sizeof(c2_lnet_core_opaque_ptr_t) == sizeof(void *));

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
struct c2_lnet_core_bev_queue {
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
	/** Linkage in the TM buffer event queue */
	struct c2_lnet_core_bev_link  lcbe_tm_link;

	/** Pointer to the transport private data (transport address space) */
	struct c2_lnet_core_buffer  *lcbe_core_pvt;

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
   Core transfer machine data.  The transport layer should embed this at the
   <b>very end</b> of its private data.  The size is determined at run time.
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
	struct c2_lnet_core_bev_queue lctm_free_bevq;

	/**
	   Buffer completion event queue.  The queue is shared between the
	   transport address space and the kernel.
	 */
	struct c2_lnet_core_bev_queue lctm_completed_bevq;
};

/**
   Core buffer data.  The transport layer should embed this at the <b>very
   end</b> of its private data.  The size varies if the buffer is used for
   receiving messages.
*/
struct c2_lnet_core_buffer {
	/**
	   The value of the c2_net_buffer address in the transport address
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
	   Active bulk buffers set the address of the remote passive transfer
	   machine in this field.
	 */
	struct c2_lnet_core_ep_addr lcb_passive_addr;

	void                 *lcb_upvt; /**< Core user space private */
	void                 *lcb_kpvt; /**< Core kernel space private */
};


/**
   Allocate and initialize the network domain's private field for use by LNet.
   @param dom The network domain pointer.
   @param lcom The private data pointer for the domain to be initialized.
 */
extern int c2_lnet_core_dom_init(struct c2_net_domain *dom,
				 struct c2_lnet_core_domain *lcdom);

/**
   Release LNet transport resources related to the domain.
 */
extern int c2_lnet_core_dom_fini(struct c2_lnet_core_domain *lcdom);

/**
   Get the maximum buffer size (counting all segments).
 */
extern c2_bcount_t c2_lnet_core_get_max_buffer_size(
                                             struct c2_lnet_core_domain *lcdom);

/**
   Get the maximum size of a buffer segment.
 */
extern c2_bcount_t c2_lnet_core_get_max_buffer_segment_size(
                                             struct c2_lnet_core_domain *lcdom);

/**
   Get the maximum number of buffer segments.
 */
extern int32_t c2_lnet_core_get_max_buffer_segments(
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
extern int c2_lnet_core_buf_register(struct c2_lnet_core_domain *lcdom,
				     struct c2_net_buffer *buf,
				     struct c2_lnet_core_buffer *lcbuf);

/**
   Deregister the buffer.
   @param The buffer private data.
 */
extern int c2_lnet_core_buf_deregister(struct c2_lnet_core_buffer *lcbuf);

/**
   Enqueue a buffer for message reception. Multiple messages may be received
   into the buffer, space permitting, up to the configured maximum.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @param lcaddr LNet end point address of the recepient.
   @pre The buffer is queued on the specified transfer machine.
 */
extern int c2_lnet_core_buf_msg_recv(struct c2_lnet_core_transfer_mc *lctm,
				     struct c2_lnet_core_buffer *lcbuf);

/**
   Enqueue a buffer for message transmission.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @param lcaddr LNet end point address of the recepient.
   @pre The buffer is queued on the specified transfer machine.
 */
extern int c2_lnet_core_buf_msg_send(struct c2_lnet_core_transfer_mc *lctm,
				     struct c2_lnet_core_buffer *lcbuf,
				     struct c2_lnet_core_ep_addr *lcaddr);

/**
   Enqueue a buffer for active bulk receive.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @param lcaddr LNet end point address of the TM with the passive buffer.
   @param match_bits The match bits of the passive buffer.
   @pre The buffer is queued on the specified transfer machine.
 */
extern int c2_lnet_core_buf_active_recv(struct c2_lnet_core_transfer_mc *lctm,
					struct c2_lnet_core_buffer *lcbuf,
					struct c2_lnet_core_ep_addr *lcaddr,
					uint64_t match_bits);

/**
   Enqueue a buffer for active bulk send.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @param lcaddr LNet end point address of the TM with the passive buffer.
   @param match_bits The match bits of the passive buffer.
   @pre The buffer is queued on the specified transfer machine.
 */
extern int c2_lnet_core_buf_active_send(struct c2_lnet_core_transfer_mc *lctm,
					struct c2_lnet_core_buffer *lcbuf,
					struct c2_lnet_core_ep_addr *lcaddr,
					uint64_t match_bits);

/**
   Enqueue a buffer for passive bulk receive.
   This is a synchronous operation.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @param match_bits Returns the match bits identifying the passive buffer.
   @pre The buffer is queued on the specified transfer machine.
 */
extern int c2_lnet_core_buf_passive_recv(struct c2_lnet_core_transfer_mc *lctm,
					 struct c2_lnet_core_buffer *lcbuf,
					 uint64_t *match_bits);

/**
   Enqueue buffer for passive bulk send.
   This is a synchronous operation.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @param match_bits Returns the match bits identifying the passive buffer.
   @pre The buffer is queued on the specified transfer machine.
 */
extern int c2_lnet_core_buf_passive_send(struct c2_lnet_core_transfer_mc *lctm,
					 struct c2_lnet_core_buffer *lcbuf,
					 uint64_t *match_bits);

/**
   Cancel a buffer operation if possible.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
 */
extern int c2_lnet_core_buf_del(struct c2_lnet_core_transfer_mc *lctm,
				struct c2_lnet_core_buffer *lcbuf);

/**
   Subroutine to wait for buffer events, or the timeout.
   @param lctm Transfer machine private data.
   @param timeout Absolute time at which to stop waiting.
   @retval 0 Events present.
   @retval -ETIMEDOUT Timed out.
 */
extern int c2_lnet_core_buf_event_wait(struct c2_lnet_core_transfer_mc *lctm,
				       c2_time_t timeout);

/**
   Fetch the next event from the circular buffer event queue.
   @param lctm Transfer machine private data.
   @param lcbe Returns the next buffer event.
   @retval true Event returned.
   @retval false No events on the queue.
 */
extern bool c2_lnet_core_buf_event_get(struct c2_lnet_core_transfer_mc *lctm,
				       struct c2_lnet_buffer_event *lcbe);

/**
   Subroutine to parse an end point address string and convert to internal form.
   A "*" value for the transfer machine identifier results in a value of
   C2_NET_LNET_TMID_INVALID being set.
 */
extern int c2_lnet_core_ep_addr_decode(struct c2_lnet_core_domain *lcdom,
				       const char *ep_addr,
				       struct c2_lnet_core_ep_addr *lcepa);

/**
   Subroutine to construct the external address string from its internal form.
   A value of C2_NET_LNET_TMID_INVALID for the lcepa_tmid field results in
   a "*" being set for that field.
 */
extern void c2_lnet_core_ep_addr_encode(struct c2_lnet_core_domain *lcdom,
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
extern int c2_lnet_core_tm_start(struct c2_net_transfer_mc *tm,
				 struct c2_lnet_core_transfer_mc *lctm,
				 struct c2_lnet_core_ep_addr *lcepa);

/**
   Stop the transfer machine and release associated resources.  All operations
   must be finalized prior to this call.
   @param lctm The transfer machine private data.
   @note There is no equivalent of the xo_tm_fini() subroutine.
 */
extern int c2_lnet_core_tm_stop(struct c2_lnet_core_transfer_mc *lctm);

/**
   The transport should invoke this subroutine before initiating any buffer
   operation, to state how many buffer event structures are required to complete
   all current and to-be-scheduled buffer operations.

   The core may have to allocate additional buffer event structures on the
   c2_lnet_core_transfer_mc::lctm_free_bevq.
 */
extern int c2_lnet_core_tm_bev_needed(struct c2_lnet_core_transfer_mc *lctm,
				      uint32_t needed);

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
