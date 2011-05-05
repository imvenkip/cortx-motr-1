/* -*- C -*- */
#ifndef __COLIBRI_NET_BULK_MEM_XPRT_H__
#define __COLIBRI_NET_BULK_MEM_XPRT_H__

#include "lib/atomic.h"
#include "lib/thread.h"
#include "net/bulk_mem.h"

/**
   @addtogroup bulkmem

   This transport can be used directly for testing purposes.  It permits the
   creation of multiple domains, each with multiple transfer machines, and
   provides support for message passing and bulk data transfer between transfer
   machines of different domains.  Data is copied between buffers.

   It uses a pool of background threads per transfer machine to provide the
   required threading semantics of the messaging API and the illusion of
   independent domains.

   It can also serve as a "base" module for derived transports that wish
   to reuse the threading and buffer management support of this module.
   Derivation is done as follows:
   - Define derived versions of data structures as required,
     with the corresponding base data structure embedded within.
   - Override the xo_ domain operations and reuse those that apply - in some
     cases the base xo_ routines can be called from their derived versions.
     The following must be replaced:
        - xo_dom_init()
	- xo_buf_add()
	- xo_end_point_create()

   - The derived xo_dom_init() subroutine should allocate the private domain
     structure, set the value in the nd_xprt_private field, and then call the
     base domain initialization subroutine.
     On return, adjust the following:
        - Size values in the base domain private structure
	- The number of threads in a transfer machine pool
	- Worker functions, especially for the following opcodes:
          C2_NET_XOP_STATE_CHANGE, C2_NET_XOP_MSG_SEND, C2_NET_XOP_ACTIVE_BULK.
          New worker functions can invoke the base worker functions if
          desired.

   - The xo_buf_add() subroutine must do what it takes to prepare a buffer
     for an operation.  This is domain specific.  The base
     version of the subroutine can be called to handle the queuing.

   - The xo_end_point_create() subroutine must do what it takes to create
     a domain specific end point. The base subroutine may be used if there
     is value in its parsing of IP address and port number; it is capable of
     allocating sufficient space in the end point data structure for the
     derived transport.

   See \ref bulksunrpc for an example of a derived transport.

   See <a href="https://docs.google.com/a/xyratex.com/document/d/1tm_IfkSsW6zfOxQlPMHeZ5gjF1Xd0FAUHeGOaNpUcHA/edit?hl=en#">RPC Bulk Transfer Task Plan</a>
   for details on the implementation.

   @{
*/

struct c2_net_bulk_mem_domain_pvt;
struct c2_net_bulk_mem_tm_pvt;
struct c2_net_bulk_mem_buffer_pvt;
struct c2_net_bulk_mem_end_point;
struct c2_net_bulk_mem_work_item;

/**
   The worker threads associated with a transfer machine perform units
   of work described by the opcodes in this enumeration.
 */
enum c2_net_bulk_mem_work_opcode {
	/** Perform a transfer machine state change operation */
	C2_NET_XOP_STATE_CHANGE=0,
	/** Perform a buffer operation cancellation callback */
	C2_NET_XOP_CANCEL_CB,
	/** perform a message received callback */
	C2_NET_XOP_MSG_RECV_CB,
	/** permform a message send operation and callback */
	C2_NET_XOP_MSG_SEND,
	/** perform a passive bulk buffer completion callback */
	C2_NET_XOP_PASSIVE_BULK_CB,
	/** perform an active bulk buffer transfer operation and callback */
	C2_NET_XOP_ACTIVE_BULK,

	C2_NET_XOP_NR
};

/**
   The internal state of transfer machine in this transport is described
   by this enumeration.

   Note these are similar to but not the same as the external state of the
   transfer machine - the external state changes only through the state
   change callback.
 */
enum c2_net_bulk_mem_tm_state {
	C2_NET_XTM_UNDEFINED   = C2_NET_TM_UNDEFINED,
	C2_NET_XTM_INITIALIZED = C2_NET_TM_INITIALIZED,
	C2_NET_XTM_STARTING    = C2_NET_TM_STARTING,
	C2_NET_XTM_STARTED     = C2_NET_TM_STARTED,
	C2_NET_XTM_STOPPING    = C2_NET_TM_STOPPING,
	C2_NET_XTM_STOPPED     = C2_NET_TM_STOPPED,
	C2_NET_XTM_FAILED      = C2_NET_TM_FAILED,
};

/**
   This structure is used to describe a work item. The structures are
   queued on a list associated with the transfer machine.
   Usually the structures are embedded in the buffer private data,
   but they will be explicitly allocated for non-buffer related work items,
   and must be freed.
 */
struct c2_net_bulk_mem_work_item {
	/** transfer machine work list link */
	struct c2_list_link                 xwi_link;

	/** Work opcode. All opcodes other than C2_NET_XOP_STATE_CHANGE
	    and C2_NET_XOP_NR relate to buffers.
	*/
	enum c2_net_bulk_mem_work_opcode    xwi_op;

	/** The next state value for a C2_NET_XOP_STATE_CHANGE opcode */
	enum c2_net_bulk_mem_tm_state       xwi_next_state;
};

/**
   Buffer private data.
*/
struct c2_net_bulk_mem_buffer_pvt {
	/** Points back to its buffer */
	struct c2_net_buffer                *xb_buffer;

	/** Work item linked on the transfer machine work list */
	struct c2_net_bulk_mem_work_item     xb_wi;

	/** Buffer id. This is set each time the buffer is used for
	    a passive bulk transfer operation.
	*/
	int64_t                              xb_buf_id;
};

/**
   Transfer machine private data.
*/
struct c2_net_bulk_mem_tm_pvt {
	/** The transfer machine pointer */
	struct c2_net_transfer_mc        *xtm_tm;
	/** Internal state of the transfer machine */
	enum c2_net_bulk_mem_tm_state     xtm_state;
	/** Prioritized list of pending work items */
	struct c2_list                    xtm_work_list;
	/** Condition variable for the work item list */
	struct c2_cond                    xtm_work_list_cv;
	/** Array of worker threads allocated during startup */
	struct c2_thread                 *xtm_worker_threads;
	/** Number of worker threads */
	size_t                            xtm_num_workers;
};

/**
   End point. It tracks an IP/port number address.
*/
enum {
	C2_NET_BULK_MEM_XEP_MAGIC    = 0x6e455064696f746eULL,
	C2_NET_BULK_MEM_XEP_ADDR_LEN = 36
};
struct c2_net_bulk_mem_end_point {
	/** Magic constant to validate end point */
	uint64_t                 xep_magic;

	/** Socket address */
	struct sockaddr_in       xep_sa;

	/** Service id. Set to 0 in the in-memory transport but usable
	    in derived transports.
	*/
	uint32_t                 xep_service_id;

	/** Externally visible end point in the TM. */
	struct c2_net_end_point  xep_ep;

	/** Storage for the printable address */
	char                     xep_addr[C2_NET_BULK_MEM_XEP_ADDR_LEN];
};

/**
   Work functions are invoked from worker threads. There is one
   work function per work item opcode.  Each function has a
   signature described by this typedef.
 */
typedef void (*c2_net_bulk_mem_work_fn_t)(struct c2_net_transfer_mc *tm,
					  struct c2_net_bulk_mem_work_item *wi);

typedef int (*c2_mem_ep_create_fn_t)(struct c2_net_end_point **epp,
				     struct c2_net_domain *dom,
				     struct sockaddr_in *sa,
				     uint32_t id);
typedef void (*c2_mem_ep_release_fn_t)(struct c2_ref *ref);
typedef void (*c2_mem_wi_add_fn_t)(struct c2_net_bulk_mem_work_item *wi,
				   struct c2_net_bulk_mem_tm_pvt *tp);
typedef bool (*c2_mem_buffer_in_bounds_fn_t)(struct c2_net_buffer *nb);
typedef int  (*c2_mem_desc_create_fn_t)(struct c2_net_buf_desc *desc,
					struct c2_net_end_point *ep,
					struct c2_net_transfer_mc *tm,
					enum c2_net_queue_type qt,
					c2_bcount_t buflen,
					int64_t buf_id);
/**
   These subroutines are exposed by the transport as they may need to be
   intercepted by a derived transport.
*/
struct c2_net_bulk_mem_ops {
	/** Subroutine to create an end point. */
	c2_mem_ep_create_fn_t        bmo_ep_create;

	/** Subroutine to release an end point. */
	c2_mem_ep_release_fn_t       bmo_ep_release;

	/** Subroutine to add a work item to the work list */
	c2_mem_wi_add_fn_t           bmo_wi_add;

	/** Subroutine to check if a buffer size is within bounds */
	c2_mem_buffer_in_bounds_fn_t bmo_buffer_in_bounds;

	/** Subroutine to create a buffer descriptor */
	c2_mem_desc_create_fn_t      bmo_desc_create;
};

/**
   Domain private data structure.
   The fields of this structure can be reset by a derived transport
   after xo_dom_init() method is called on the in-memory transport.
 */
struct c2_net_bulk_mem_domain_pvt {
	/** Domain pointer */
	struct c2_net_domain      *xd_dom;

	/** Work functions. */
	c2_net_bulk_mem_work_fn_t  xd_work_fn[C2_NET_XOP_NR];

	/** Exposed subroutines - derived transports may need to intercept */
	struct c2_net_bulk_mem_ops xd_ops;

	/**
	   Size of the end point structure.
	   Initialized to the size of c2_net_bulk_mem_end_point.
	 */
	size_t                     xd_sizeof_ep;

	/**
	   Size of the transfer machine private data.
	   Initialized to the size of c2_net_bulk_mem_tm_pvt.
	 */
	size_t                     xd_sizeof_tm_pvt;

	/**
	   Size of the buffer private data.
	   Initialized to the size of c2_net_buf_emul_buf_pvt.
	 */
	size_t                     xd_sizeof_buffer_pvt;

	/**
	   Number of tuples in the address.
	 */
	size_t                     xd_addr_tuples;

	/**
	   Number of threads in a transfer machine pool.
	*/
	size_t                     xd_num_tm_threads;

	/**
	   Linkage of in-memory c2_net_domain objects for in-memory
	   communication.
	   This is only done if the xd_derived is false.
	 */
	struct c2_list_link        xd_dom_linkage;

	/**
	   Indicator of a derived transport.
	   Will be set to true if the transport pointer provided to
	   the xo_dom_init() method is not c2_net_bulk_mem_xprt.
	 */
	bool                       xd_derived;

	/**
	   Counter for passive bulk buffer identifiers.
	*/
	struct c2_atomic64         xd_buf_id_counter;
};

#ifdef MEM_WI_TO_BUFFER
#undef MEM_WI_TO_BUFFER
#endif
/**
   Macro to obtain the c2_net_buffer pointer from its related work item.
@code
struct c2_net_buffer *nb = MEM_WI_TO_BUFFER(wi);
@endcode
 */
#define MEM_WI_TO_BUFFER(wi)						\
({									\
	struct c2_net_bulk_mem_buffer_pvt *bp;				\
	bp = container_of(wi, struct c2_net_bulk_mem_buffer_pvt, xb_wi);\
	bp->xb_buffer;							\
})

#ifdef MEM_BUFFER_TO_WI
#undef MEM_BUFFER_TO_WI
#endif
/**
   Macro to obtain the work item from a c2_net_buffer pointer.
@code
struct c2_net_bulk_mem_work_item *wi = MEM_BUFFER_TO_WI(nb)
@endcode
 */
#define MEM_BUFFER_TO_WI(buf)			\
({						\
	struct c2_net_bulk_mem_buffer_pvt *bp;	\
	bp = buf->nb_xprt_private;		\
	&bp->xb_wi;				\
})

#ifdef MEM_EP_ADDR
#undef MEM_EP_ADDR
#endif
/**
   Macro to return the IP address of the end point.
   @param ep End point pointer
   @retval address In network byte order.
 */
#define MEM_EP_ADDR(ep)							\
({									\
	struct c2_net_bulk_mem_end_point *mep =				\
		container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep); \
	mep->xep_sa.sin_addr.s_addr;					\
 })

#ifdef MEM_EP_PORT
#undef MEM_EP_PORT
#endif
/**
   Macro to return the port number of the end point.
   @param ep End point pointer
   @retval port In network byte order.
 */
#define MEM_EP_PORT(ep)							\
({									\
	struct c2_net_bulk_mem_end_point *mep =				\
		container_of(ep, struct c2_net_bulk_mem_end_point, xep_ep); \
	mep->xep_sa.sin_port;						\
 })

/**
   @}
*/


#endif /* __COLIBRI_NET_BULK_MEM_XPRT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
