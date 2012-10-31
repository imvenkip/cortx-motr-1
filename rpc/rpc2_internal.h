#pragma once

#ifndef __COLIBRI_RPC_INT_H__
#define __COLIBRI_RPC_INT_H__

#include "rpc/conn_internal.h"
#include "rpc/session_internal.h"
#include "rpc/slot_internal.h"
#include "rpc/item_internal.h"
#include "rpc/rpc_machine_internal.h"
#include "rpc/formation2_internal.h"
#include "rpc/packet_internal.h"
#include "rpc/session_ff.h"
#include "rpc/session_fops.h"
#include "rpc/session_foms.h"
#include "rpc/rpc_onwire.h"
#include "rpc/rpc_onwire_xc.h"

/**
   Initialises all the session related fop types
 */
int c2_rpc_session_module_init(void);

/**
   Finalises all session realted fop types
 */
void c2_rpc_session_module_fini(void);

/**
   Called for each received item.
   If item is request then
	APPLY the item to proper slot
   else
	report REPLY_RECEIVED to appropriate slot
 */
int c2_rpc_item_received(struct c2_rpc_item    *item,
			 struct c2_rpc_machine *machine);

/**
   Helper to create cob

   @param dom cob domain in which cob should be created.
   @param pcob parent cob in which new cob is to be created
   @param name name of cob
   @param out newly created cob
   @param tx transaction context

   @return 0 on success. *out != NULL
 */

int c2_rpc_cob_create_helper(struct c2_cob_domain *dom,
			     struct c2_cob        *pcob,
			     const char           *name,
			     struct c2_cob       **out,
			     struct c2_db_tx      *tx);

/**
   Lookup a cob named 'name' in parent cob @pcob. If found store reference
   in @out. If not found set *out to NULL. To lookup root cob, pcob can be
   set to NULL
 */
int c2_rpc_cob_lookup_helper(struct c2_cob_domain *dom,
			     struct c2_cob        *pcob,
			     const char           *name,
			     struct c2_cob       **out,
			     struct c2_db_tx      *tx);


/**
  Lookup /SESSIONS entry in cob namespace
 */
int c2_rpc_root_session_cob_get(struct c2_cob_domain *dom,
				 struct c2_cob      **out,
				 struct c2_db_tx     *tx);

/**
  Creates /SESSIONS entry in cob namespace
 */
int c2_rpc_root_session_cob_create(struct c2_cob_domain *dom,
				   struct c2_db_tx      *tx);

/**
   Helper routine, internal to rpc module.
   Sets up and posts rpc-item representing @fop.
 */
int c2_rpc__fop_post(struct c2_fop                *fop,
		     struct c2_rpc_session        *session,
		     const struct c2_rpc_item_ops *ops);

/**
   Temporary routine to place fop in a global queue, from where it can be
   selected for execution.
 */
void c2_rpc_item_dispatch(struct c2_rpc_item *item);

bool c2_rpc_item_is_control_msg(const struct c2_rpc_item *item);

#endif /* __COLIBRI_RPC_INT_H__ */
