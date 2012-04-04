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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 11/01/2011
 *
 */
#ifndef __COLIBRI_NET_LNET_CORE_H__
#define __COLIBRI_NET_LNET_CORE_H__

/**
   @page LNetCoreDLD-fspec LNet Transport Core API

   - @ref LNetCoreDLD-fspec-ovw
   - @ref LNetCoreDLD-fspec-ds
   - @ref LNetCoreDLD-fspec-subs
   - @ref LNetCore "LNet Transport Core Interfaces"

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

   The API subroutines are described in
   @ref LNetCore "LNet Transport Core Interfaces".
   The subroutines are categorized as follows:

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
     - nlx_core_buf_desc_encode()
     - nlx_core_buf_desc_decode()
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
   @see @ref LNetDRVDLD "LNet Transport Device DLD"

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
struct nlx_core_buf_desc;
struct page;

/**
   @defgroup LNetCore LNet Transport Core Interfaces
   @ingroup LNetDFS

   The internal, address space agnostic I/O API used by the LNet transport.
   See @ref LNetCoreDLD-fspec "LNet Transport Core API" for organizational
   details and @ref LNetDLD "LNet Transport DLD" for details of the
   Colibri Network transport for LNet.

   @{
 */

#ifndef NLX_SCOPE
#define NLX_SCOPE static
#endif

/**
   Opaque type wide enough to represent an address in any address space.
 */
typedef uint64_t nlx_core_opaque_ptr_t;
C2_BASSERT(sizeof(nlx_core_opaque_ptr_t) >= sizeof(void *));

/**
   This structure defines the fields in an LNet transport end point address.
   It is packed to minimize the network descriptor size.
 */
struct nlx_core_ep_addr {
	uint64_t cepa_nid;    /**< The LNet Network Identifier */
	uint32_t cepa_pid;    /**< The LNet Process Identifier */
	uint32_t cepa_portal; /**< The LNet Portal Number */
	uint32_t cepa_tmid;   /**< The Transfer Machine Identifier */
} __attribute__((__packed__));

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
	/** Buffer match bit mask */
	C2_NET_LNET_BUFFER_ID_MASK = C2_NET_LNET_BUFFER_ID_MAX,
};
C2_BASSERT(C2_NET_LNET_TMID_BITS + C2_NET_LNET_BUFFER_ID_BITS <= 64);

/* Magic numbers */
enum {
	C2_NET_LNET_CORE_BUF_MAGIC = 0x436f7265427566ULL, /* CoreBuf */
	C2_NET_LNET_CORE_TM_MAGIC  = 0x436f7265544dULL,   /* CoreTM */
};

/**
 * An kernel memory location, in terms of page and offset.
 */
struct nlx_core_kmem_loc {
	union {
		struct {
			/** Page containing the object. */
			struct page *kl_page;
			/** Offset of the object in the page. */
			uint32_t     kl_offset;
		} __attribute__((__packed__));
		uint32_t     kl_data[3];
	};
	/** A checksum of the page and offset, to detect corruption. */
	uint32_t     kl_checksum;
};
C2_BASSERT(sizeof(((struct nlx_core_kmem_loc*) NULL)->kl_page) +
	   sizeof(((struct nlx_core_kmem_loc*) NULL)->kl_offset) ==
	   sizeof(((struct nlx_core_kmem_loc*) NULL)->kl_data));

enum {
	/** Maximum size of an LNET NID string, same as LNET_NIDSTR_SIZE */
	C2_NET_LNET_NIDSTR_SIZE = 32,
};

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
	   Pointer to the next element in the consumer address space.
	 */
	nlx_core_opaque_ptr_t cbl_c_next;

	/**
	   Self reference in the producer (kernel).
	   The producer reference is kept in the form of a nlx_core_kmem_loc
	   so that queue elements do not all need to be mapped.
	 */
	struct nlx_core_kmem_loc cbl_p_self_loc;

	/**
	   Reference to the next element in the producer.
	   The next reference is kept in the form of a nlx_core_kmem_loc so
	   that queue elements do not all need to be mapped.
	 */
	struct nlx_core_kmem_loc cbl_p_next_loc;
};

/**
   Buffer event queue, operable from either kernel and user space
   with a single producer and single consumer.
 */
struct nlx_core_bev_cqueue {
	/** Number of elements currently in the queue. */
	size_t                 cbcq_nr;

	/** Number of elements in the queue that can be consumed. */
	struct c2_atomic64     cbcq_count;

	/**
	   The consumer removes elements from this anchor.
	   The consumer pointer value is in the address space of the
	   consumer (transport).
	 */
	nlx_core_opaque_ptr_t cbcq_consumer;

	/**
	   The producer adds links to this anchor.
	   The producer reference is kept in the form of a nlx_core_kmem_loc
	   so that queue elements do not all need to be mapped.
	 */
	struct nlx_core_kmem_loc cbcq_producer_loc;
};

enum {
	/** Minimum number of buffer event entries in the queue. */
	C2_NET_LNET_BEVQ_MIN_SIZE  = 2,
	/** Number of reserved buffer event entries in the queue.
	    The entry pointed to by the consumer is owned by the consumer and
	    thus cannot be used by the producer.
	    It will eventually be used when the pointers advance.
	 */
	C2_NET_LNET_BEVQ_NUM_RESERVED = 1,
};

/**
   This structure describes a buffer event. It is very similar to
   struct c2_net_buffer_event.
 */
struct nlx_core_buffer_event {
	/** Linkage in the TM buffer event queue */
	struct nlx_core_bev_link     cbe_tm_link;

	/**
	    This value is set by the kernel Core module's LNet event handler,
	    and is copied from the nlx_core_buffer::cb_buffer_id
	    field. The value is a pointer to the c2_net_buffer structure in the
	    transport address space.
	 */
	nlx_core_opaque_ptr_t        cbe_buffer_id;

	/** Event timestamp */
	c2_time_t                    cbe_time;

	/** Status code (-errno). 0 is success */
	int32_t                      cbe_status;

	/** Length of data in the buffer */
	c2_bcount_t                  cbe_length;

	/** Offset of start of the data in the buffer. (Receive only) */
	c2_bcount_t                  cbe_offset;

	/** Address of the other end point.  (unsolicited Receive only)  */
	struct nlx_core_ep_addr      cbe_sender;

	/** True if the buffer is no longer in use */
        bool                         cbe_unlinked;

	/** Core kernel space private. */
	void                        *cbe_kpvt;
};

/**
   Core domain data.  The transport layer should embed this in its private data.
 */
struct nlx_core_domain {
	void    *cd_upvt; /**< Core user space private */
	void    *cd_kpvt; /**< Core kernel space private */
	unsigned _debug_;
};

/**
   Core transfer machine data.  The transport layer should embed this in its
   private data.
 */
struct nlx_core_transfer_mc {
	uint64_t                   ctm_magic;

	/** The transfer machine address. */
	struct nlx_core_ep_addr    ctm_addr;

	/** Boolean indicating if the transport is running in user space. */
	bool                       ctm_user_space_xo;

	/**
	   Buffer completion event queue.  The queue is shared between the
	   transport address space and the kernel.
	 */
	struct nlx_core_bev_cqueue ctm_bevq;

	/**
	   Count of bevq entries needed. Incremented by each nlx_xo_buf_add()
	   operation (not necessarily by 1), and decremented when the
	   buffer is unlinked by LNet, in nlx_xo_bev_deliver_all().
	 */
	size_t                     ctm_bev_needed;

	/** Match bit counter.
	    Range [C2_NET_LNET_BUFFER_ID_MIN, C2_NET_LNET_BUFFER_ID_MAX].
	*/
	uint64_t                   ctm_mb_counter;

	void                      *ctm_upvt; /**< Core user space private */
	void                      *ctm_kpvt; /**< Core kernel space private */

	unsigned                   _debug_;
};

/**
   Core buffer data.  The transport layer should embed this in its private data.
 */
struct nlx_core_buffer {
	uint64_t                cb_magic;

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
	   The length of data involved in the operation.
	   Note this is less than or equal to the buffer length.
	 */
	c2_bcount_t             cb_length;

	/**
	   Value from nb_min_receive_size for receive queue buffers only.
	 */
	c2_bcount_t             cb_min_receive_size;

	/**
	   Value from nb_max_receive_msgs for receive queue buffers.
	   Set to 1 in other cases.
	   The value is used for the threshold field of an lnet_md_t, and
	   specifies the number of internal buffer event structures that
	   have to be provisioned to accommodate the expected result
	   notifications.
	 */
	uint32_t                cb_max_operations;

	/**
	   The match bits for a passive bulk buffer, including the TMID field.
	   They should be set using the nlx_core_buf_desc_encode()
	   subroutine.

	   The field is also used in an active buffer to describe the match
	   bits of the remote passive buffer.

	   The field is set automatically for receive buffers.
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
   The LNet transport's Network Buffer Descriptor format.
   The external form is the opaque c2_net_buf_desc.
   All fields are stored in little-endian order, and the structure is
   copied as-is to the external opaque form.
 */
struct nlx_core_buf_desc {
	union {
		struct {
			/** Match bits of the passive buffer */
			uint64_t                 cbd_match_bits;

			/** Passive TM's end point */
			struct nlx_core_ep_addr  cbd_passive_ep;

			/** Passive buffer queue type (enum c2_net_queue_type)
			    expressed here explicitly as a 32 bit number.
			*/
			uint32_t                 cbd_qtype;

			/** Passive buffer size */
			c2_bcount_t              cbd_size;
		};
		uint64_t         cbd_data[5];
	};
	uint64_t         cbd_checksum;
};

/**
   Allocates and initializes the network domain's private field for use by LNet.
   @param dom The network domain pointer.
   @param lcdom The private data pointer for the domain to be initialized.
 */
NLX_SCOPE int nlx_core_dom_init(struct c2_net_domain *dom,
				struct nlx_core_domain *lcdom);

/**
   Releases LNet transport resources related to the domain.
 */
NLX_SCOPE void nlx_core_dom_fini(struct nlx_core_domain *lcdom);

/**
   Gets the maximum buffer size (counting all segments).
 */
NLX_SCOPE c2_bcount_t nlx_core_get_max_buffer_size(
						struct nlx_core_domain *lcdom);

/**
   Gets the maximum size of a buffer segment.
 */
NLX_SCOPE c2_bcount_t nlx_core_get_max_buffer_segment_size(
						struct nlx_core_domain *lcdom);

/**
   Gets the maximum number of buffer segments.
 */
NLX_SCOPE int32_t nlx_core_get_max_buffer_segments(
						struct nlx_core_domain *lcdom);

/**
   Registers a network buffer.  In user space this results in the buffer memory
   getting pinned.
   The subroutine allocates private data to associate with the network buffer.
   @param lcdom The domain private data to be initialized.
   @param buffer_id Value to set in the cb_buffer_id field.
   @param bvec Buffer vector with core address space pointers.
   @param lcbuf The core private data pointer for the buffer.
 */
NLX_SCOPE int nlx_core_buf_register(struct nlx_core_domain *lcdom,
				    nlx_core_opaque_ptr_t buffer_id,
				    const struct c2_bufvec *bvec,
				    struct nlx_core_buffer *lcbuf);

/**
   Deregisters the buffer.
   @param lcdom The domain private data.
   @param lcbuf The buffer private data.
 */
NLX_SCOPE void nlx_core_buf_deregister(struct nlx_core_domain *lcdom,
				       struct nlx_core_buffer *lcbuf);

/**
   Enqueues a buffer for message reception. Multiple messages may be received
   into the buffer, space permitting, up to the configured maximum.

   The invoker should ensure that the subroutine is not invoked concurrently
   with any of the other buffer operation initiation subroutines or the
   nlx_core_buf_event_get() subroutine.

   The invoker should provision sufficient buffer event structures prior to
   the call, using the nlx_core_bevq_provision() subroutine.

   @param lcdom The domain private data.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre lcbuf->cb_length is valid
   @pre lcbuf->cb_min_receive_size is valid
   @pre lcbuf->cb_max_operations > 0
   @see nlx_core_bevq_provision()
 */
NLX_SCOPE int nlx_core_buf_msg_recv(struct nlx_core_domain *lcdom,
				    struct nlx_core_transfer_mc *lctm,
				    struct nlx_core_buffer *lcbuf);

/**
   Enqueues a buffer for message transmission.

   The invoker should ensure that the subroutine is not invoked concurrently
   with any of the other buffer operation initiation subroutines or the
   nlx_core_buf_event_get() subroutine.

   The invoker should provision sufficient buffer event structures prior to
   the call, using the nlx_core_bevq_provision() subroutine.

   @param lcdom The domain private data.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre lcbuf->cb_length is valid
   @pre lcbuf->cb_addr is valid
   @pre lcbuf->cb_max_operations == 1
   @see nlx_core_bevq_provision()
 */
NLX_SCOPE int nlx_core_buf_msg_send(struct nlx_core_domain *lcdom,
				    struct nlx_core_transfer_mc *lctm,
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

   The invoker should provision sufficient buffer event structures prior to
   the call, using the nlx_core_bevq_provision() subroutine.

   @param lcdom The domain private data.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
   @pre lcbuf->cb_length is valid
   @pre lcbuf->cb_match_bits != 0
   @pre lcbuf->cb_addr is valid
   @pre lcbuf->cb_max_operations == 1
   @see nlx_core_bevq_provision()
 */
NLX_SCOPE int nlx_core_buf_active_recv(struct nlx_core_domain *lcdom,
				       struct nlx_core_transfer_mc *lctm,
				       struct nlx_core_buffer *lcbuf);

/**
   Enqueues a buffer for active bulk send.
   See nlx_core_buf_active_recv() for how the buffer is to be initialized.

   The invoker should ensure that the subroutine is not invoked concurrently
   with any of the other buffer operation initiation subroutines or the
   nlx_core_buf_event_get() subroutine.

   The invoker should provision sufficient buffer event structures prior to
   the call, using the nlx_core_bevq_provision() subroutine.

   @param lcdom The domain private data.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
   @pre lcbuf->cb_length is valid
   @pre lcbuf->cb_match_bits != 0
   @pre lcbuf->cb_addr is valid
   @pre lcbuf->cb_max_operations == 1
   @see nlx_core_bevq_provision()
 */
NLX_SCOPE int nlx_core_buf_active_send(struct nlx_core_domain *lcdom,
				       struct nlx_core_transfer_mc *lctm,
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
   @param cbd Descriptor structure to be filled in.
   @pre The buffer is queued on the specified transfer machine on one of the
   passive bulk queues.
   @see nlx_core_buf_desc_decode()
 */
NLX_SCOPE void nlx_core_buf_desc_encode(struct nlx_core_transfer_mc *lctm,
					struct nlx_core_buffer *lcbuf,
					struct nlx_core_buf_desc *cbd);

/**
   This subroutine decodes the buffer descriptor and copies the values into the
   given core buffer private data.  It is the inverse operation of the
   nlx_core_buf_desc_encode().

   It does the following:
   - The descriptor is validated.
   - The cb_addr field and cb_match_bits fields are set from the descriptor,
     providing the address of the passive buffer.
   - The operation being performed (SEND or RECV) is validated against the
     descriptor.
   - The active buffer length is validated against the passive buffer.
   - The size of the active transfer is set in the cb_length field.

   @param lctm  Transfer machine private data.
   @param lcbuf The buffer private data with cb_length set to the buffer size.
   @param cbd Descriptor structure to be filled in.
   @retval -EINVAL Invalid descriptor
   @retval -EPERM  Invalid operation
   @retval -EFBIG  Buffer too small
   @pre The buffer is queued on the specified transfer machine on one of the
   active bulk queues.
   @see nlx_core_buf_desc_encode()
 */
NLX_SCOPE int nlx_core_buf_desc_decode(struct nlx_core_transfer_mc *lctm,
				       struct nlx_core_buffer *lcbuf,
				       struct nlx_core_buf_desc *cbd);

/**
   Enqueues a buffer for passive bulk receive.
   The match bits for the passive buffer should be set in the buffer with the
   nlx_core_buf_desc_encode() subroutine before this call.
   It is guaranteed that the buffer can be remotely accessed when the
   subroutine returns.

   The invoker should ensure that the subroutine is not invoked concurrently
   with any of the other buffer operation initiation subroutines or the
   nlx_core_buf_event_get() subroutine.

   The invoker should provision sufficient buffer event structures prior to
   the call, using the nlx_core_bevq_provision() subroutine.

   @param lcdom The domain private data.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
   @pre lcbuf->cb_length is valid
   @pre lcbuf->cb_match_bits != 0
   @pre lcbuf->cb_max_operations == 1
   @see nlx_core_bevq_provision()
 */
NLX_SCOPE int nlx_core_buf_passive_recv(struct nlx_core_domain *lcdom,
					struct nlx_core_transfer_mc *lctm,
					struct nlx_core_buffer *lcbuf);

/**
   Enqueues a buffer for passive bulk send.
   See nlx_core_buf_passive_recv() for how the buffer is to be initialized.
   It is guaranteed that the buffer can be remotely accessed when the
   subroutine returns.

   The invoker should ensure that the subroutine is not invoked concurrently
   with any of the other buffer operation initiation subroutines or the
   nlx_core_buf_event_get() subroutine.

   The invoker should provision sufficient buffer event structures prior to
   the call, using the nlx_core_bevq_provision() subroutine.

   @param lcdom The private data pointer for the domain.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
   @pre lcbuf->cb_length is valid
   @pre lcbuf->cb_match_bits != 0
   @pre lcbuf->cb_max_operations == 1
   @see nlx_core_bevq_provision()
 */
NLX_SCOPE int nlx_core_buf_passive_send(struct nlx_core_domain *lcdom,
					struct nlx_core_transfer_mc *lctm,
					struct nlx_core_buffer *lcbuf);

/**
   Cancels a buffer operation if possible.
   @param lcdom The domain private data.
   @param lctm  Transfer machine private data.
   @param lcbuf Buffer private data.
   @pre The buffer is queued on the specified transfer machine.
 */
NLX_SCOPE int nlx_core_buf_del(struct nlx_core_domain *lcdom,
			       struct nlx_core_transfer_mc *lctm,
			       struct nlx_core_buffer *lcbuf);

/**
   Waits for buffer events, or the timeout.
   @param lcdom Domain pointer.
   @param lctm Transfer machine private data.
   @param timeout Absolute time at which to stop waiting.  A value of 0
   indicates that the subroutine should not wait.
   @retval 0 Events present.
   @retval -ETIMEDOUT Timed out before events arrived.
 */
NLX_SCOPE int nlx_core_buf_event_wait(struct nlx_core_domain *lcdom,
				      struct nlx_core_transfer_mc *lctm,
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
   @see nlx_core_bevq_provision()
 */
NLX_SCOPE bool nlx_core_buf_event_get(struct nlx_core_transfer_mc *lctm,
				      struct nlx_core_buffer_event *lcbe);

/**
   Parses an end point address string and convert to internal form.
   A "*" value for the transfer machine identifier results in a value of
   C2_NET_LNET_TMID_INVALID being set.
 */
NLX_SCOPE int nlx_core_ep_addr_decode(struct nlx_core_domain *lcdom,
				      const char *ep_addr,
				      struct nlx_core_ep_addr *cepa);

/**
   Constructs the external address string from its internal form.
   A value of C2_NET_LNET_TMID_INVALID for the cepa_tmid field results in
   a "*" being set for that field.
 */
NLX_SCOPE void nlx_core_ep_addr_encode(struct nlx_core_domain *lcdom,
				       const struct nlx_core_ep_addr *cepa,
				       char buf[C2_NET_LNET_XEP_ADDR_LEN]);

/**
   Gets a list of strings corresponding to the local LNET network interfaces.
   The returned array must be released using nlx_core_nidstrs_put().
   @param nidary A NULL-terminated (like argv) array of NID strings is returned.
 */
NLX_SCOPE int nlx_core_nidstrs_get(struct nlx_core_domain *lcdom,
				   char * const **nidary);

/**
   Releases the string array returned by nlx_core_nidstrs_get().
 */
NLX_SCOPE void nlx_core_nidstrs_put(struct nlx_core_domain *lcdom,
				    char * const **nidary);

/**
   Starts a transfer machine. Internally this results in
   the creation of the LNet EQ associated with the transfer machine.
   @param lcdom The domain private data.
   @param tm The transfer machine pointer.
   @param lctm The transfer machine private data to be initialized.  The
   nlx_core_transfer_mc::ctm_addr must be set by the caller.  If the
   lcpea_tmid field value is C2_NET_LNET_TMID_INVALID then a transfer machine
   identifier is dynamically assigned to the transfer machine and the
   nlx_core_transfer_mc::ctm_addr is modified in place.
   @note There is no equivalent of the xo_tm_init() subroutine.
   @note This function does not create a c2_net_end_point for the transfer
   machine, because there is no equivalent object at the core layer.
 */
NLX_SCOPE int nlx_core_tm_start(struct nlx_core_domain *lcdom,
				struct c2_net_transfer_mc *tm,
				struct nlx_core_transfer_mc *lctm);

/**
   Stops the transfer machine and release associated resources.  All operations
   must be finalized prior to this call.
   @param lcdom The domain private data.
   @param lctm The transfer machine private data.
   @note There is no equivalent of the xo_tm_fini() subroutine.
 */
NLX_SCOPE void nlx_core_tm_stop(struct nlx_core_domain *lcdom,
				struct nlx_core_transfer_mc *lctm);

/**
   Compare two struct nlx_core_ep_addr objects.
 */
static inline bool nlx_core_ep_eq(const struct nlx_core_ep_addr *cep1,
				  const struct nlx_core_ep_addr *cep2)
{
	return cep1->cepa_nid == cep2->cepa_nid &&
		cep1->cepa_pid == cep2->cepa_pid &&
		cep1->cepa_portal == cep2->cepa_portal &&
		cep1->cepa_tmid == cep2->cepa_tmid;
}

/**
   Subroutine to provision additional buffer event entries on the
   buffer event queue if needed.
   It increments the struct nlx_core_transfer_mc::ctm_bev_needed counter
   by the number of LNet events that can be delivered, as indicated by the
   @c need parameter.

   The subroutine is to be used in the consumer address space only, and uses
   a kernel or user space specific allocator subroutine to obtain an
   appropriately blessed entry in the producer space.

   The invoker must lock the transfer machine prior to this call.

   @param lcdom LNet core domain pointer.
   @param lctm Pointer to LNet core TM data structure.
   @param need Number of additional buffer entries required.
   @see nlx_core_new_blessed_bev(), nlx_core_bevq_release()
 */
NLX_SCOPE int nlx_core_bevq_provision(struct nlx_core_domain *lcdom,
				      struct nlx_core_transfer_mc *lctm,
				      size_t need);

/**
   Subroutine to reduce the needed capacity of the buffer event queue.
   Note: Entries are never actually released from the circular queue until
   termination.

   The subroutine is to be used in the consumer address space only.
   The invoker must lock the transfer machine prior to this call.

   @param lctm Pointer to LNet core TM data structure.
   @param release Number of buffer entries released.
   @see nlx_core_bevq_provision()
 */
NLX_SCOPE void nlx_core_bevq_release(struct nlx_core_transfer_mc *lctm,
				     size_t release);

/**
   Subroutine to allocate a new buffer event structure initialized
   with the producer space self pointer set.
   This subroutine is defined separately for the kernel and user space.
   @param lcdom LNet core domain pointer.
   @param lctm LNet core transfer machine pointer.
   In the user space transport this must be initialized at least with the
   core device driver file descriptor.
   In kernel space this is not used.
   @param bevp Buffer event return pointer.  The memory must be allocated with
   the NLX_ALLOC_PTR() macro or variant.  It will be freed with the
   NLX_FREE_PTR() macro from the nlx_core_bev_free_cb() subroutine.
   @post bev_cqueue_bless(&bevp->cbe_tm_link) has been invoked.
   @see bev_cqueue_bless()
 */
NLX_SCOPE int nlx_core_new_blessed_bev(struct nlx_core_domain *lcdom,
				       struct nlx_core_transfer_mc *lctm,
				       struct nlx_core_buffer_event **bevp);

/**
   Allocate zero-filled memory, like c2_alloc().
   In user space, this memory is allocated such that it will not
   cross page boundaries using c2_alloc_aligned().
   @param size Memory size.
   @param shift Alignment, ignored in kernel space.
   @pre size <= PAGE_SIZE
 */
NLX_SCOPE void *nlx_core_mem_alloc(size_t size, unsigned shift);

/**
   Frees memory allocated by nlx_core_mem_alloc().
 */
NLX_SCOPE void nlx_core_mem_free(void *data, size_t size, unsigned shift);

NLX_SCOPE void nlx_core_dom_set_debug(struct nlx_core_domain *lcdom,
				      unsigned dbg);
NLX_SCOPE void nlx_core_tm_set_debug(struct nlx_core_transfer_mc *lctm,
				     unsigned dbg);

/**
   Round up a number n to the next power of 2, min 1<<3, works for n <= 1<<9.
   If n is a power of 2, returns n.
   Requires a constant input, allowing compile-time computation.
 */
#define NLX_PO2_SHIFT(n)                                                \
	(((n) <= 8) ? 3 : ((n) <= 16) ? 4 : ((n) <= 32) ? 5 :           \
	 ((n) <= 64) ? 6 : ((n) <= 128) ? 7 : ((n) <= 256) ? 8 :        \
	 ((n) <= 512) ? 9 : ((n) / 0))
#define NLX_ALLOC_PTR(ptr) \
	((ptr) = nlx_core_mem_alloc(sizeof ((ptr)[0]),                  \
				    NLX_PO2_SHIFT(sizeof ((ptr)[0]))))
#define NLX_ALLOC_PTR_ADDB(ptr, ctx, loc) \
	if (NLX_ALLOC_PTR(ptr) == NULL) C2_ADDB_ADD(ctx, loc, c2_addb_oom)
#define NLX_FREE_PTR(ptr) \
	nlx_core_mem_free((ptr), sizeof ((ptr)[0]), \
                          NLX_PO2_SHIFT(sizeof ((ptr)[0])))

/** @} */ /* LNetCore */

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
