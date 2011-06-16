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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 04/12/2011
 */
#ifndef __COLIBRI_NET_BULK_SUNRPC_XPRT_H__
#define __COLIBRI_NET_BULK_SUNRPC_XPRT_H__

#include "net/bulk_sunrpc.h"
#include "net/bulk_emulation/mem_xprt.h"


#ifdef __KERNEL__
#include "net/bulk_emulation/sunrpc_io_k.h"
#else
#include <arpa/inet.h>
#include "net/bulk_emulation/sunrpc_io_u.h"
#endif

/**
   @addtogroup bulksunrpc

   This module is derived from the @ref bulkmem module.

   See <a href="https://docs.google.com/a/xyratex.com/document/d/1tm_IfkSsW6zfOxQlPMHeZ5gjF1Xd0FAUHeGOaNpUcHA/edit?hl=en#">RPC Bulk Transfer Task Plan</a>
   for details on the implementation.

   @{
*/

struct c2_net_bulk_sunrpc_domain_pvt;
struct c2_net_bulk_sunrpc_tm_pvt;
struct c2_net_bulk_sunrpc_end_point;
struct c2_net_bulk_sunrpc_buffer_pvt;

enum {
	C2_NET_BULK_SUNRPC_XDP_MAGIC  = 0x53756e7270634450ULL,
	C2_NET_BULK_SUNRPC_XTM_MAGIC  = 0x53756e727063544dULL,
	C2_NET_BULK_SUNRPC_XEP_MAGIC  = 0x53756e7270634550ULL,
	C2_NET_BULK_SUNRPC_XBP_MAGIC  = 0x53756e7270634250ULL,
	C2_NET_BULK_SUNRPC_TM_THREADS = 2,
	C2_NET_BULK_SUNRPC_MAX_BUFFER_SIZE     = (1<<20),
	C2_NET_BULK_SUNRPC_MAX_SEGMENT_SIZE    = (1<<20),
	C2_NET_BULK_SUNRPC_MAX_BUFFER_SEGMENTS = 256,
	C2_NET_BULK_SUNRPC_EP_DELAY_S = 20, /* in seconds */
};

/** Domain private data. */
struct c2_net_bulk_sunrpc_domain_pvt {
	uint64_t                          xd_magic;

	/** The in-memory base domain */
	struct c2_net_bulk_mem_domain_pvt xd_base;

	/** Pointer to in-mem methods */
	const struct c2_net_bulk_mem_ops *xd_base_ops;

	/** The user or kernel space sunrpc domain */
        struct c2_net_domain              xd_rpc_dom;

	/** The delay before releasing an EP */
	c2_time_t                         xd_ep_release_delay;

	/** The skulker thread */
	struct c2_thread                  xd_skulker_thread;

	/** Skulker CV */
	struct c2_cond                    xd_skulker_cv;

	/** Skulker control */
	bool                              xd_skulker_run;

	/** Skulker heart beat counter (for UT) */
	uint32_t                          xd_skulker_hb;
};

/**
   Recover the sunrpc domain private pointer from a pointer to the domain.
 */
static inline struct c2_net_bulk_sunrpc_domain_pvt *
sunrpc_dom_to_pvt(const struct c2_net_domain *dom)
{
	struct c2_net_bulk_mem_domain_pvt *mdp = mem_dom_to_pvt(dom);
	return container_of(mdp, struct c2_net_bulk_sunrpc_domain_pvt, xd_base);
}


/** Buffer private data. */
struct c2_net_bulk_sunrpc_buffer_pvt {
	uint64_t                          xsb_magic;

	/** The in-memory base private data */
	struct c2_net_bulk_mem_buffer_pvt xsb_base;
};

/**
   Recover the sunrpc buffer private pointer from a pointer to the
   buffer.
 */
static inline struct c2_net_bulk_sunrpc_buffer_pvt *
sunrpc_buffer_to_pvt(const struct c2_net_buffer *nb)
{
	struct c2_net_bulk_mem_buffer_pvt *mbp = mem_buffer_to_pvt(nb);
	return container_of(mbp, struct c2_net_bulk_sunrpc_buffer_pvt,
			    xsb_base);
}

/** Transfer machine private data */
struct c2_net_bulk_sunrpc_tm_pvt {
	uint64_t                      xtm_magic;

	/** The in-memory base private data */
	struct c2_net_bulk_mem_tm_pvt xtm_base;

	struct c2_list_link           xtm_tm_linkage;
};

/**
   Recover the sunrpc TM private pointer from a pointer to the TM.
 */
static inline struct c2_net_bulk_sunrpc_tm_pvt *
sunrpc_tm_to_pvt(const struct c2_net_transfer_mc *tm)
{
	struct c2_net_bulk_mem_tm_pvt *mtp = mem_tm_to_pvt(tm);
	return container_of(mtp, struct c2_net_bulk_sunrpc_tm_pvt, xtm_base);
}

/**
   End point of the transport.  It embeds a service id and a network
   connection.

   The network connection creation is deferred until first
   use, because the end point data structure is also passed to a TM
   during start up, and the c2_service structure only requires the
   service id - indeed it would not be possible to create the connection
   to the yet undefined service.
 */
struct c2_net_bulk_sunrpc_end_point {
	uint64_t                         xep_magic;

	/** The in-memory base end point */
	struct c2_net_bulk_mem_end_point xep_base;

	/** Indicator that xep_sid has been initialized */
	bool                             xep_sid_valid;

	/** Indicator that a connection has been created
	    for the sid in the underlying transport.
	 */
	bool                             xep_conn_created;

	/** Service id */
	struct c2_service_id             xep_sid;

	/** The time the internal sunrpc connection was last used.
	    The field is used to control the duration of end point caching.
	    A value of C2_NEVER implies that the connection encountered
	    an error and hence the end point should not be cached.

	    The value is maintained as an atomic variable to avoid
	    the need to acquire the domain mutex when setting the value.
	 */
	struct c2_atomic64               xep_last_use;
};

/**
   Recover the sunrpc end point private pointer from a pointer to the end point.
 */
static inline struct c2_net_bulk_sunrpc_end_point *
sunrpc_ep_to_pvt(const struct c2_net_end_point *ep)
{
	struct c2_net_bulk_mem_end_point *mep = mem_ep_to_pvt(ep);
	return container_of(mep, struct c2_net_bulk_sunrpc_end_point, xep_base);
}

#ifdef __KERNEL__
int sunrpc_buffer_init(struct sunrpc_buffer *sb, void *buf, size_t len);
void sunrpc_buffer_fini(struct sunrpc_buffer *sb);
#endif

int c2_sunrpc_fop_init(void);
void c2_sunrpc_fop_fini(void);

/**
   @}
*/

#endif /* __COLIBRI_NET_BULK_SUNRPC_XPRT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
