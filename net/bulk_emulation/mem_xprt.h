/* -*- C -*- */
#ifndef __COLIBRI_NET_BULK_MEM_XPRT_H__
#define __COLIBRI_NET_BULK_MEM_XPRT_H__

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
};

/**
   Transfer machine private data.
*/
struct c2_net_bulk_mem_tm_pvt {
	struct c2_net_transfer_mc        *xtm_tm;
	enum c2_net_bulk_mem_tm_state     xtm_state;
	struct c2_list                    xtm_work_list;
	struct c2_cond                    xtm_work_list_cv;
	struct c2_thread                 *xtm_worker_threads;
	size_t                            xtm_num_workers;
};

/**
   End point. It tracks an IP/port number address.
*/
enum {
	C2_NET_XEP_MAGIC = 0x6e455064696f746eULL,
};
struct c2_net_bulk_mem_end_point {
	/** Magic constant to validate end point */
	uint64_t                 xep_magic;

	/** Socket address */
	struct sockaddr_in       xep_sa;

	/** Externally visible end point in the TM. */
	struct c2_net_end_point  xep_ep;
};

/**
   Work functions are invoked from worker threads. There is one
   work function per work item opcode.  Each function has a
   signature described by this typedef.
 */
typedef void (*c2_net_bulk_mem_work_fn_t)(struct c2_net_transfer_mc *tm,
					  struct c2_net_bulk_mem_work_item *wi);

/**
   Domain private data structure.
   The fields of this structure can be reset by a derived transort
   after xo_dom_init() method is called on the in-memory transport.
 */
struct c2_net_bulk_mem_domain_pvt {
	/** Domain pointer */
	struct c2_net_domain      *xd_dom;

        /** Work functions. */
	c2_net_bulk_mem_work_fn_t  xd_work_fn[C2_NET_XOP_NR];

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
};


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
