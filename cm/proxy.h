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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 *                  Subhash Arya  <subhash_arya@xyratex.com>
 * Original creation date: 03/08/2013
 */

#pragma once

#ifndef __MERO_CM_PROXY_H__
#define __MERO_CM_PROXY_H__

#include "lib/chan.h"

#include "fop/fom_simple.h"
#include "rpc/conn.h"
#include "rpc/session.h"
#include "sm/sm.h"

#include "cm/sw.h"
#include "cm/ag.h"

/**
   @defgroup CMPROXY copy machine proxy
   @ingroup CM

   @{
*/

struct m0_cm_cp;

/**
 * Represents remote replica and stores its details including its sliding
 * window.
 */
struct m0_cm_proxy {
	/** Remote replica's identifier. */
	uint64_t               px_id;

	/** Remote replica's sliding window. */
	struct m0_cm_sw        px_sw;

	/** Last local sliding window update sent to this replica. */
	struct m0_cm_sw        px_last_sw_onwire_sent;

	struct m0_sm_ast       px_sw_onwire_ast;
	bool                   px_is_done;

	uint64_t               px_nr_asts;

	/** Back reference to local copy machine. */
	struct m0_cm          *px_cm;

	/**
	 * Pending list of copy packets to be forwarded to the remote
	 * replica.
	 * @see m0_cm_cp::c_proxy_linkage
	 */
	struct m0_tl           px_pending_cps;

	struct m0_mutex        px_mutex;

	struct m0_rpc_conn     px_conn;

	struct m0_rpc_session  px_session;

	const char            *px_endpoint;

	/**
	 * Linkage into copy machine proxy list.
	 * @see struct m0_cm::cm_proxies
	 */
	struct m0_tlink        px_linkage;

	uint64_t               px_magic;
};

/**
 * Sliding window update fop context for a remote replica proxy.
 * @see m0_cm_proxy_remote_update()
 */
struct m0_cm_proxy_sw_onwire {
	struct m0_fop       pso_fop;
	/**
	 * Remote copy machine replica proxy to which the sliding window
	 * update FOP is to be sent (i.e. m0_cm_proxy_sw_onwire_fop::psu_fop).
	 */
	struct m0_cm_proxy *pso_proxy;
};

M0_INTERNAL int m0_cm_proxy_alloc(uint64_t px_id,
				  struct m0_cm_ag_id *lo,
				  struct m0_cm_ag_id *hi,
				  const char *endpoint,
				  struct m0_cm_proxy **pxy);

M0_INTERNAL void m0_cm_proxy_add(struct m0_cm *cm, struct m0_cm_proxy *pxy);

M0_INTERNAL void m0_cm_proxy_del(struct m0_cm *cm, struct m0_cm_proxy *pxy);

M0_INTERNAL struct m0_cm_proxy *m0_cm_proxy_locate(struct m0_cm *cm,
						   const char *ep);

M0_INTERNAL void m0_cm_proxy_update(struct m0_cm_proxy *pxy,
				    struct m0_cm_ag_id *lo,
				    struct m0_cm_ag_id *hi);

M0_INTERNAL int m0_cm_proxy_remote_update(struct m0_cm_proxy *proxy,
					  struct m0_cm_sw *sw);

M0_INTERNAL void m0_cm_proxy_cp_add(struct m0_cm_proxy *pxy,
				    struct m0_cm_cp *cp);

M0_INTERNAL uint64_t m0_cm_proxy_nr(struct m0_cm *cm);

M0_INTERNAL void m0_cm_proxy_rpc_conn_close(struct m0_cm_proxy *pxy);

M0_INTERNAL bool m0_cm_proxy_agid_is_in_sw(struct m0_cm_proxy *pxy,
					   struct m0_cm_ag_id *id);

M0_INTERNAL void m0_cm_proxy_fini(struct m0_cm_proxy *pxy);

/**
 * Checks if m0_cm_proxy::px_nr_updates_sent == m0_cm_proxy::px_nr_asts, else
 * waits on channel m0_cm_proxy::px_signal. Once all the call backs
 * corresponding to the updates sent are processed m0_cm_proxy::px_signal is
 * notified. This avoids invalid state of proxy during proxy finalisation.
 * @see proxy_sw_onwire_ast_cb()
 * @see m0_cm_proxies_fini()
 */
M0_INTERNAL void m0_cm_proxy_fini_wait(struct m0_cm_proxy *proxy);

M0_TL_DESCR_DECLARE(proxy, M0_EXTERN);
M0_TL_DECLARE(proxy, M0_INTERNAL, struct m0_cm_proxy);

M0_TL_DESCR_DECLARE(proxy_cp, M0_EXTERN);
M0_TL_DECLARE(proxy_cp, M0_INTERNAL, struct m0_cm_cp);

/** @} endgroup CMPROXY */

/* __MERO_CM_PROXY_H__ */

#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
