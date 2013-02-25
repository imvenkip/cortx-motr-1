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
 * Original author: Amit Jambure <amit_jambure@xyratex.com>
 * Original creation date: 10/31/2012
 */

#pragma once

#ifndef __MERO_RPC_INT_H__
#define __MERO_RPC_INT_H__

#include "addb/addb.h"
#include "rpc/conn_internal.h"
#include "rpc/session_internal.h"
#include "rpc/slot_internal.h"
#include "rpc/item_internal.h"
#include "rpc/rpc_machine_internal.h"
#include "rpc/formation2_internal.h"
#include "rpc/packet_internal.h"
#include "rpc/session_fops_xc.h"
#include "rpc/session_fops.h"
#include "rpc/session_foms.h"
#include "rpc/rpc_onwire.h"
#include "rpc/rpc_onwire_xc.h"
#include "rpc/rpc.h"
#include "rpc/rpc_addb.h"

extern struct m0_addb_ctx m0_rpc_addb_ctx;

/**
 *  RPC function failure macro using the global ADDB machine to post.
 *  @param rc Return code
 *  @param loc Location code - one of the M0_RPC_ADDB_LOC_ enumeration
 *             constants suffixes from rpc/rpc_addb.h.
 *  @param ctx Runtime context pointer
 *  @pre rc < 0
 */
#define RPC_ADDB_FUNCFAIL(rc, loc, ctx)					\
	M0_ADDB_FUNC_FAIL(&m0_addb_gmc, M0_RPC_ADDB_LOC_##loc, rc, ctx)

#define RPC_ALLOC(ptr, len, loc, ctx)					\
	M0_ALLOC_ADDB(ptr, len, &m0_addb_gmc, M0_RPC_ADDB_LOC_##loc, ctx)
#define RPC_ALLOC_PTR(ptr, loc, ctx)					\
	M0_ALLOC_PTR_ADDB(ptr, &m0_addb_gmc, M0_RPC_ADDB_LOC_##loc, ctx)
#define RPC_ALLOC_ARR(ptr, nr, loc, ctx)				\
	M0_ALLOC_ARR_ADDB(ptr, nr, &m0_addb_gmc, M0_RPC_ADDB_LOC_##loc, ctx)

/**
 * @addtogroup rpc
 * @{
 */

#define REQH_ADDB_MC_CONFIGURED(reqh)					\
        reqh != NULL && m0_addb_mc_is_fully_configured(&reqh->rh_addb_mc)

/**
   Initialises all the session related fop types
 */
M0_INTERNAL int m0_rpc_session_module_init(void);

/**
   Finalises all session realted fop types
 */
M0_INTERNAL void m0_rpc_session_module_fini(void);

/**
   Called for each received item.
   If item is request then
	APPLY the item to proper slot
   else
	report REPLY_RECEIVED to appropriate slot
 */
M0_INTERNAL int m0_rpc_item_received(struct m0_rpc_item *item,
				     struct m0_rpc_machine *machine);

/**
   Helper to create cob

   @param dom cob domain in which cob should be created.
   @param pcob parent cob in which new cob is to be created
   @param name name of cob
   @param out newly created cob
   @param tx transaction context

   @return 0 on success. *out != NULL
 */

M0_INTERNAL int m0_rpc_cob_create_helper(struct m0_cob_domain *dom,
					 const struct m0_cob *pcob,
					 const char *name,
					 struct m0_cob **out,
					 struct m0_db_tx *tx);

/**
   Lookup a cob named 'name' in parent cob @pcob. If found store reference
   in @out. If not found set *out to NULL. To lookup root cob, pcob can be
   set to NULL
 */
M0_INTERNAL int m0_rpc_cob_lookup_helper(struct m0_cob_domain *dom,
					 struct m0_cob *pcob,
					 const char *name,
					 struct m0_cob **out,
					 struct m0_db_tx *tx);

/**
  Lookup /SESSIONS entry in cob namespace
 */
M0_INTERNAL int m0_rpc_root_session_cob_get(struct m0_cob_domain *dom,
					    struct m0_cob **out,
					    struct m0_db_tx *tx);

/**
  Creates /SESSIONS entry in cob namespace
 */
int m0_rpc_root_session_cob_create(struct m0_cob_domain *dom,
				   struct m0_db_tx *tx);

M0_INTERNAL void rpc_worker_thread_fn(struct m0_rpc_machine *machine);

/**
   Helper routine, internal to rpc module.
   Sets up and posts rpc-item representing @fop.
 */
M0_INTERNAL int m0_rpc__fop_post(struct m0_fop *fop,
				 struct m0_rpc_session *session,
				 const struct m0_rpc_item_ops *ops,
				 m0_time_t abs_timeout);

/**
   Temporary routine to place fop in a global queue, from where it can be
   selected for execution.
 */
M0_INTERNAL void m0_rpc_item_dispatch(struct m0_rpc_item *item);

M0_INTERNAL bool m0_rpc_item_is_control_msg(const struct m0_rpc_item *item);

M0_INTERNAL void m0_rpc_oneway_item_post_locked(const struct m0_rpc_conn *conn,
						struct m0_rpc_item *item);

M0_TL_DESCR_DECLARE(item_source, M0_EXTERN);
M0_TL_DECLARE(item_source, M0_INTERNAL, struct m0_rpc_item_source);

/** @} */

#endif /* __MERO_RPC_INT_H__ */
