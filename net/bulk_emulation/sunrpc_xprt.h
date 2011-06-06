/* -*- C -*- */
#ifndef __COLIBRI_NET_BULK_SUNRPC_XPRT_H__
#define __COLIBRI_NET_BULK_SUNRPC_XPRT_H__

#include "net/bulk_sunrpc.h"
#include "net/bulk_emulation/mem_xprt.h"

#include <arpa/inet.h>

#ifdef __KERNEL__
#include "net/bulk_emulation/sunrpc_io_k.h"
#else
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
