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
 * Original creation date: 11/10/2011
 *
 */
#ifndef __COLIBRI_NET_LNET_XO_H__
#define __COLIBRI_NET_LNET_XO_H__

#include "net/lnet/lnet_core.h"
#include "lib/thread.h"

/**
   @defgroup LNetXODFS LNet Transport XO Interface
   @ingroup LNetDFS
   @{
*/

struct nlx_xo_buf_desc;
struct nlx_xo_buffer;
struct nlx_xo_domain;
struct nlx_xo_ep;
struct nlx_xo_transfer_mc;

/**
   LNet transport's internal end point structure.
 */
struct nlx_xo_ep {
	/** embedded network end point structure. */
	struct c2_net_end_point xe_ep;

	/** LNet transport address */
	struct nlx_core_ep_addr xe_core;

	/** Memory for the string representation of the end point.
	    The @c xe_ep.nep_addr field points to @c xe_addr.
	*/
	char                    xe_addr[C2_NET_LNET_XEP_ADDR_LEN];
};

/**
   Internal form of the LNet transport's Network Buffer Descriptor.
   The external form is the opaque c2_net_buf_desc.
 */
struct nlx_xo_buf_desc {
	/** Match bits of the passive buffer */
        uint64_t                 xbd_match_bits;

	/** Passive TM's end point */
        struct nlx_core_ep_addr  xbd_passive_ep;

	/** Passive buffer queue type */
        enum c2_net_queue_type   xbd_qtype;

	/** Passive buffer size */
        c2_bcount_t              xbd_size;
};

/**
   Private data pointed to by c2_net_domain::nd_xprt_private.
 */
struct nlx_xo_domain {
	/** Pointer back to the network dom */
	struct c2_net_domain   *xd_dom;

	/** LNet Core transfer domain data (shared memory) */
	struct nlx_core_domain  xd_core;
};

/**
   Private data pointed to by c2_net_transfer_mc::ntm_xprt_private.
 */
struct nlx_xo_transfer_mc {
	/** Pointer back to the network tm */
	struct c2_net_transfer_mc   *xtm_tm;

	/** Transfer machine thread processor affinity */
	struct c2_bitmap             xtm_processors;

	/** Event thread */
	struct c2_thread             xtm_ev_thread;

	/** Condition variable used by the event thread for synchronous buffer
	    event notification.
	 */
	struct c2_cond               xtm_ev_cond;

	/** Channel used for synchronous buffer event notification */
	struct c2_chan              *xtm_ev_chan;

	/** Count of activities in progress out of the TM mutex */
	int                          xtm_busy;

	/** LNet Core transfer machine data (shared memory) */
	struct nlx_core_transfer_mc  xtm_core;
};

/**
   Private data pointed to by c2_net_buffer::nb_xprt_private.
 */
struct nlx_xo_buffer {
	/** Pointer back to the network buffer */
	struct c2_net_buffer   *xb_nb;

	/** LNet Core buffer data (shared memory) */
	struct nlx_core_buffer  xb_core;
};


/**
   @} LNetXODFS
*/

#endif /* __COLIBRI_NET_LNET_XO_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
