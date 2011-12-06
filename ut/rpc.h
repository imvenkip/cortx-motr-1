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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 11/23/2011
 */

#ifndef __COLIBRI_UT_RPC_H__
#define __COLIBRI_UT_RPC_H__


struct c2_rpc_ctx;

/**
  A wrapper around c2_rpc_server_start(). It initializes dbenv and cob_domain
  withing c2_rpc_ctx structure, and then calls c2_rpc_server_start().

  @param rctx  Initialized rpc context structure.
*/
int c2_rpc_server_init(struct c2_rpc_ctx *rctx);

/**
  A wrapper around c2_rpc_server_stop(). It finalizes dbenv and cob_domain
  withing c2_rpc_ctx structure, and then calls c2_rpc_server_stop().

  @param rctx  Initialized rpc context structure.
*/
void c2_rpc_server_fini(struct c2_rpc_ctx *rctx);

/**
  A wrapper around c2_rpc_client_start(). It initializes dbenv and cob_domain
  withing c2_rpc_ctx structure, and then calls c2_rpc_client_start().

  @param rctx  Initialized rpc context structure.
*/
int c2_rpc_client_init(struct c2_rpc_ctx *rctx);

/**
  A wrapper around c2_rpc_client_stop(). It finalizes dbenv and cob_domain
  withing c2_rpc_ctx structure, and then calls c2_rpc_client_stop().

  @param rctx  Initialized rpc context structure.
*/
int c2_rpc_client_fini(struct c2_rpc_ctx *rctx);

#endif /* __COLIBRI_UT_RPC_H__ */

