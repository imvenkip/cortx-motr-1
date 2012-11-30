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
 * Original author: Nikita_Danilov <Nikita_Danilov@xyratex.com>
 *		    Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 04/28/2011
 */

/**
   @defgroup rpc RPC
   @page rpc-layer-core-dld RPC layer core DLD
   @section Overview
   RPC layer core is used to transmit rpc items and groups of them.

   @see https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMz
V6NzJfMTljbTZ3anhjbg&hl=en

   @{
*/

#pragma once

#ifndef __COLIBRI_RPC_RPCCORE_H__
#define __COLIBRI_RPC_RPCCORE_H__

#include "lib/arith.h"                /* max32u */
#include "rpc/rpc_machine.h"
#include "rpc/conn.h"
#include "rpc/session.h"
#include "rpc/item.h"
#include "rpc/bulk.h"
#include "rpc/service.h"
#include "rpc/rpc_helpers.h"
#include "net/buffer_pool.h"
#include "rpc/item.h"        /* c2_rpc_item_onwire_header_size() */

/** @todo Add these declarations to some internal header */
extern const struct c2_addb_ctx_type c2_rpc_addb_ctx_type;
extern const struct c2_addb_loc      c2_rpc_addb_loc;
extern       struct c2_addb_ctx      c2_rpc_addb_ctx;

C2_INTERNAL int c2_rpc_init(void);
C2_INTERNAL void c2_rpc_fini(void);

/**
 * Calculates the total number of buffers needed in network domain for
 * receive buffer pool.
 * @param len total Length of the TM's in a network domain
 * @param tms_nr    Number of TM's in the network domain
 */
C2_INTERNAL uint32_t c2_rpc_bufs_nr(uint32_t len, uint32_t tms_nr);

/** Returns the maximum segment size of receive pool of network domain. */
C2_INTERNAL c2_bcount_t c2_rpc_max_seg_size(struct c2_net_domain *ndom);

/** Returns the maximum number of segments of receive pool of network domain. */
C2_INTERNAL uint32_t c2_rpc_max_segs_nr(struct c2_net_domain *ndom);

/** Returns the maximum RPC message size in the network domain. */
C2_INTERNAL c2_bcount_t c2_rpc_max_msg_size(struct c2_net_domain *ndom,
					    c2_bcount_t rpc_size);

/**
 * Returns the maximum number of messages that can be received in a buffer
 * of network domain for a specific maximum receive message size.
 */
C2_INTERNAL uint32_t c2_rpc_max_recv_msgs(struct c2_net_domain *ndom,
					  c2_bcount_t rpc_size);

/**
  Posts an unbound item to the rpc layer.

  The item will be sent through one of item->ri_session slots.

  The rpc layer will try to send the item out not later than
  item->ri_deadline and with priority of item->ri_priority.

  If this call returns without errors, the item's reply call-back is
  guaranteed to be called eventually.

  After successful call to c2_rpc_post(), user should not free the item.
  Rpc-layer will internally free the item when rpc-layer is sure that the item
  will not take part in recovery.

  Rpc layer does not provide any API, to "wait until reply is received".
  Upon receiving reply to item, item->ri_chan is signaled.
  If item->ri_ops->rio_replied() callback is set, then it will be called.
  Pointer to reply item can be retrieved from item->ri_reply.
  If any error occured, item->ri_error is set to non-zero value.

  Note: setting item->ri_ops and adding clink to item->ri_chan MUST be done
  before calling c2_rpc_post(), because reply to the item can arrive even
  before c2_rpc_post() returns.

  @pre item->ri_session != NULL
  @pre item->ri_priority is sane.
*/
C2_INTERNAL int c2_rpc_post(struct c2_rpc_item *item);

/**
  Posts reply item on the same session on which the request item is received.

  After successful call to c2_rpc_reply_post(), user should not free the reply
  item. Rpc-layer will internally free the item when rpc-layer is sure that
  the corresponding request item will not take part in recovery.
 */
int c2_rpc_reply_post(struct c2_rpc_item *request, struct c2_rpc_item *reply);

C2_INTERNAL int c2_rpc_oneway_item_post(const struct c2_rpc_conn *conn,
					struct c2_rpc_item *item);

/**
   Create a buffer pool per net domain which to be shared by TM's in it.
   @pre ndom != NULL && app_pool != NULL
   @pre bufs_nr != 0
 */
C2_INTERNAL int c2_rpc_net_buffer_pool_setup(struct c2_net_domain *ndom,
					     struct c2_net_buffer_pool
					     *app_pool, uint32_t bufs_nr,
					     uint32_t tm_nr);

void c2_rpc_net_buffer_pool_cleanup(struct c2_net_buffer_pool *app_pool);

/** @} end group rpc */

#endif /* __COLIBRI_RPC_RPCCORE_H__  */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
