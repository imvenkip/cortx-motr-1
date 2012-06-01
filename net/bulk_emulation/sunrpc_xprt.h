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
struct c2_net_bulk_sunrpc_conn;

enum {
	C2_NET_BULK_SUNRPC_XDP_MAGIC  = 0x53756e7270634450ULL,
	C2_NET_BULK_SUNRPC_XTM_MAGIC  = 0x53756e727063544dULL,
	C2_NET_BULK_SUNRPC_XEP_MAGIC  = 0x53756e7270634550ULL,
	C2_NET_BULK_SUNRPC_XBP_MAGIC  = 0x53756e7270634250ULL,
	C2_NET_BULK_SUNRPC_CONN_MAGIC = 0x53756e727063436fULL,
	C2_NET_BULK_SUNRPC_TM_THREADS = 2,
	C2_NET_BULK_SUNRPC_MAX_BUFFER_SIZE     = (1 << 19),
	C2_NET_BULK_SUNRPC_MAX_SEGMENT_SIZE    = (1 << 19),
	C2_NET_BULK_SUNRPC_MAX_BUFFER_SEGMENTS = 256,
	C2_NET_BULK_SUNRPC_EP_DELAY_S = 20, /* in seconds */
	C2_NET_BULK_SUNRPC_SKULKER_PERIOD_S  = 10, /* in seconds */
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

	/** Skulker clock period */
	c2_time_t                         xd_skulker_period;

	/** Skulker forced execution */
	bool                              xd_skulker_force;

	/** Connection cache of struct c2_net_bulk_sunrpc_conn objects */
	struct c2_list                    xd_conn_cache;

	/** Connection cache CV */
	struct c2_cond                    xd_conn_cache_cv;
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

/**
   Connection cache entry maintained per domain.  It is used to
   track open connections and delay their destruction.
 */
struct c2_net_bulk_sunrpc_conn {
	/** magic */
	uint64_t                         xc_magic;

	/** reference counter */
	struct c2_ref                    xc_ref;

	/** domain pointer */
	struct c2_net_domain            *xc_dom;

	/** domain private linkage */
	struct c2_list_link              xc_dp_linkage;

	/** IP address, port number */
	struct sockaddr_in               xc_sa;

	/** flag set when long operations are in progress */
	bool                             xc_in_use;

	/** sid created flag */
	bool                             xc_sid_created;

	/** Service id used to lookup the connection */
	struct c2_service_id             xc_sid;

	/** conn created flag */
	bool                             xc_conn_created;

	/** The time the internal sunrpc connection was last used.
	    The field is used to control the duration of end point caching.
	    A value of C2_TIME_NEVER implies that the connection encountered
	    an error and hence the end point should not be cached.

	    The value is maintained as an atomic variable to avoid
	    the need to acquire the domain mutex when setting the value.
	 */
	struct c2_atomic64               xc_last_use;
};

/**
   Recover the sunrpc conn pointer from a pointer to the embedded reference.
 */
static inline struct c2_net_bulk_sunrpc_conn *
sunrpc_ref_to_conn(struct c2_ref *ref)
{
	return container_of(ref, struct c2_net_bulk_sunrpc_conn, xc_ref);
}

/**
   Populate a struct sunrpc_buffer given a buffer pointer and length.
   @pre sb != NULL
   @param sb buffer object to initialize
   @param cur start of buffer to initialize the sunrpc_buffer, or NULL.
   Cursor is moved by len bytes on success.
   @param len size of buffer.  The sunrpc_buffer is created with a buffer of
   this size.  If cur is non-NULL, its contents, up to len, are also copied
   into the buffer.
   @retval 0 (success)
   @retval -errno (failure)
 */
int sunrpc_buffer_init(struct sunrpc_buffer *sb,
		       struct c2_bufvec_cursor *cur, c2_bcount_t len);

/** release pages pinned and memory allocated by sunrpc_buffer_init */
void sunrpc_buffer_fini(struct sunrpc_buffer *sb);

/**
   Copy the contents of a sunrpc_buffer out to a c2_bufvec using a cursor.
   @pre sb != NULL && sb->sb_buf != NULL && sb->sb_pgoff < PAGE_CACHE_SIZE
   @param dest destination buffer cursor
   @param sb source sunrpc_buffer
   @retval 0 (success)
   @retval -errno (failure)
 */
int sunrpc_buffer_copy_out(struct c2_bufvec_cursor *dest,
			   const struct sunrpc_buffer *sb);

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
