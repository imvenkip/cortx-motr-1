/* -*- C -*- */
#ifndef __COLIBRI_NET_BULK_SUNRPC_XPRT_H__
#define __COLIBRI_NET_BULK_SUNRPC_XPRT_H__

#include "net/bulk_sunrpc.h"
#include "net/bulk_emulation/mem_xprt.h"

#include <arpa/inet.h>

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

/** Domain private data. */
struct c2_net_bulk_sunrpc_domain_pvt {
	/** The in-memory base domain */
	struct c2_net_bulk_emul_domain_pvt xd_base;

	/** The {uk}sunrpc domain */
        struct c2_net_domain               xd_rpc_dom;
};

/** Transfer machine private data */
struct c2_net_bulk_sunrpc_tm_pvt {
	/** The in-memory base private data */
	struct c2_net_bulk_emul_tm_pvt xtm_base;

	/** The rpc service */
	struct c2_service              xtm_service;
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
	struct c2_net_bulk_emul_end_point xep_base;

	/** Indicator that xep_sid has been initialized */
	bool                              xep_sid_valid;

	/** Indicator that xep_conn has been initialized */
	bool                              xep_conn_valid;

	/** Service id */
	struct c2_service_id              xep_sid;

	/** 
	    Network connector.  The creation of this is deferred
	    until first use. It must be protected by the
	    transfer machine mutex during creation and setting
	    of the xep_conn_valid flag.
	 */
	struct c2_net_conn                xep_conn;
};

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
