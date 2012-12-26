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

/**
 * @addtogroup rpc
 * @{
 */

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

/**
   Helper routine, internal to rpc module.
   Sets up and posts rpc-item representing @fop.
 */
M0_INTERNAL int m0_rpc__fop_post(struct m0_fop *fop,
				 struct m0_rpc_session *session,
				 const struct m0_rpc_item_ops *ops);

/**
   Temporary routine to place fop in a global queue, from where it can be
   selected for execution.
 */
M0_INTERNAL void m0_rpc_item_dispatch(struct m0_rpc_item *item);

M0_INTERNAL bool m0_rpc_item_is_control_msg(const struct m0_rpc_item *item);

/** @} */

#endif /* __MERO_RPC_INT_H__ */
