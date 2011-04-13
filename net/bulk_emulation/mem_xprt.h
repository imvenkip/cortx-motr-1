/* -*- C -*- */
#ifndef __COLIBRI_NET_BULK_MEM_PVT_H__
#define __COLIBRI_NET_BULK_MEM_PVT_H__

#include "lib/thread.h"
#include "net/bulk_mem.h"

/**
   @addtogroup bulkmem

   It can be used directly for testing purposes. It can also serve as a
   "base" module for derived transports.

   The sunrpc transport derives from this module by overriding
   the "xo_" domain operations, setting the right sizes in its domain
   and replacing the work functions
   described in the domain private data structure.  See \ref bulksunrpc 
   for details.

   See <a href="https://docs.google.com/a/xyratex.com/document/d/1tm_IfkSsW6zfOxQlPMHeZ5gjF1Xd0FAUHeGOaNpUcHA/edit?hl=en#">RPC Bulk Transfer Task Plan</a>
   for details on the implementation.

   @{
*/

struct c2_net_bulk_emul_domain_pvt;
struct c2_net_bulk_emul_tm_pvt;
struct c2_net_bulk_emul_buffer_pvt;
struct c2_net_bulk_emul_end_point;
struct c2_net_bulk_emul_work_item;

/**
   The worker threads associated with a transfer machine perform units
   of work described by the opcodes in this enumeration.
 */
enum c2_net_bulk_emul_work_opcode {
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
enum c2_net_bulk_emul_tm_state {
	C2_NET_XTM_UNDEFINED   = C2_NET_TM_UNDEFINED,
	C2_NET_XTM_INITIALIZED = C2_NET_TM_INITIALIZED,
	C2_NET_XTM_STARTING    = C2_NET_TM_STARTING,
	C2_NET_XTM_STARTED     = C2_NET_TM_STRARTED,
	C2_NET_XTM_STOPPING    = C2_NET_TM_STOPPING,
	C2_NET_XTM_STOPPED     = C2_NET_TM_STOPPED,
};

/**
   This structure is used to describe a work item. The structures are
   queued on a list associated with the transfer machine.
   Usually the structures are embedded in the buffer private data,
   but they will be explicitly allocated for non-buffer related work items,
   and must be freed.
 */
struct c2_net_bulk_emul_work_item {
	/** transfer machine work list link */
	struct c2_list_link                 xwi_link;

	/** Work opcode. All opcodes other than C2_NET_XOP_STATE_CHANGE
	    and C2_NET_XOP_NR relate to buffers. 
	*/
	enum c2_net_bulk_emul_work_opcode   xwi_op;

	/** The next state value for a C2_NET_XOP_STATE_CHANGE opcode */
	enum c2_net_bulk_emul_tm_state      xwi_next_state;
};

/**
   Buffer private data.
*/
struct c2_net_bulk_emul_buffer_pvt {
	/** Points back to its buffer */
	struct c2_net_buffer                *nb_buffer;

	/** Work item linked on the transfer machine work list */
	struct c2_net_bulk_emul_work_item    nb_wi;
};

/**
   Transfer machine private data.
*/
struct c2_net_bulk_emul_tm_pvt {
	struct c2_net_transfer_mc        *xtm_tm;
	enum c2_net_bulk_emul_tm_state    xtm_state;
	struct c2_list                    xtm_work_list;
	struct c2_cond                    xtm_work_list_cv;
	struct c2_thread                 *xtm_worker_threads;
	size_t                            xtm_num_workers;
};

/**
   End point. It tracks an IP/port number address.
*/
enum {
	C2_NET_XEP_MAGIC = 0x6e455064696f746eULL;
};
struct c2_net_bulk_emul_end_point {
	/** Magic constant to validate end point */
	uint64_t                 xep_magic;

	/** Socket address */
	struct sockaddr_in       xep_address;

	/** Externally visible end point in the TM. */
	struct c2_net_end_point  xep_ep;
};

/**
   Work functions are invoked from worker threads. There is one
   work function per work item opcode.  Each function has a
   signature described by this typedef.
 */
typedef void (*c2_net_bulk_emul_work_fn)(struct c2_net_bulk_emul_work_item *wi);

enum {
	C2_NET_XD_MAGIC = 0x6c42536b526e4350ULL;
};

/**
   Domain private data structure.
   The fields of this structure can be reset by a derived transort
   after xo_dom_init() method is called on the in-memory transport.
 */
struct c2_net_bulk_emul_domain_pvt {
	/** Magic constant to validate private data */
	uint64_t                   xd_magic;

        /** Work functions. 
	*/
	c2_net_bulk_emul_work_fn xd_work_fn[C2_NET_XOP_NR];

	/**
	   Size of the end point structure. 
	   Initialized to the size of c2_net_bulk_emul_end_point.
	 */
	size_t                   xd_sizeof_ep;

	/**
	   Size of the transfer machine private data.
	   Initialized to the size of c2_net_bulk_emul_tm_pvt.
	 */
	size_t                   xd_sizeof_tm_pvt;

	/**
	   Size of the buffer private data.
	   Initialized to the size of c2_net_buf_emul_buf_pvt.
	 */
	size_t                   xd_sizeof_buf_pvt;
};


/**
   @}
*/


#endif /* __COLIBRI_NET_BULK_MEM_PVT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
