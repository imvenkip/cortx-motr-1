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

   This module is derived from the \ref bulkmem module.

   See <a href="https://docs.google.com/a/xyratex.com/document/d/1tm_IfkSsW6zfOxQlPMHeZ5gjF1Xd0FAUHeGOaNpUcHA/edit?hl=en#">RPC Bulk Transfer Task Plan</a>
   for details on the implementation.

   @{
*/

struct c2_net_bulk_sunrpc_domain_pvt;
struct c2_net_bulk_sunrpc_tm_pvt;
struct c2_net_bulk_sunrpc_end_point;

enum {
	C2_NET_BULK_SUNRPC_XDP_MAGIC = 0x53756e7270634450ULL,
	C2_NET_BULK_SUNRPC_XTM_MAGIC = 0x53756e727063544dULL,
	C2_NET_BULK_SUNRPC_XEP_MAGIC = 0x53756e7270634550ULL,
};


/** Domain private data. */
struct c2_net_bulk_sunrpc_domain_pvt {
	/** The in-memory base domain */
	struct c2_net_bulk_mem_domain_pvt xd_base;

	uint64_t                          xd_magic;

        /** Copy of in-mem work functions. */
	c2_net_bulk_mem_work_fn_t         xd_base_work_fn[C2_NET_XOP_NR];

	/** Copy of in-mem subroutines */
	struct c2_net_bulk_mem_ops        xd_base_ops;

	/** The {uk}sunrpc domain */
        struct c2_net_domain              xd_rpc_dom;
};

/** Buffer private data. */
struct c2_net_bulk_sunrpc_buffer_pvt {
	/** The in-memory base private data */
	struct c2_net_bulk_mem_buffer_pvt xsb_base;

	/** The peer transport info, set on received operations */
	struct sockaddr_in                xsb_peer_sa;
};

/** Transfer machine private data */
struct c2_net_bulk_sunrpc_tm_pvt {
	/** The in-memory base private data */
	struct c2_net_bulk_mem_tm_pvt xtm_base;

	uint64_t                      xtm_magic;

	/** The rpc service */
	struct c2_service             xtm_service;
};

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
	/** The in-memory base end point */
	struct c2_net_bulk_mem_end_point xep_base;

	uint64_t                         xep_magic;

	/** Indicator that xep_sid has been initialized */
	bool                             xep_sid_valid;

	/** Indicator that xep_conn has been initialized */
	bool                             xep_conn_valid;

	/** Service id */
	struct c2_service_id             xep_sid;

	/**
	    Network connector.  The creation of this is deferred
	    until first use. It must be protected by the
	    transfer machine mutex during creation and setting
	    of the xep_conn_valid flag.
	 */
	struct c2_net_conn              *xep_conn;
};

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
