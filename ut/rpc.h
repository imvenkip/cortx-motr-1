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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 11/23/2011
 */

#pragma once

#ifndef __COLIBRI_UT_RPC_H__
#define __COLIBRI_UT_RPC_H__

#include "ut/cs_service.h" /* c2_cs_default_stypes, c2_cs_default_stypes_nr */
#include "rpc/rpclib.h"    /* c2_rpc_server_ctx */

#ifndef __KERNEL__
#define C2_RPC_SERVER_CTX_DEFINE(name, xprts, xprts_nr, server_argv, \
				 server_argc, service_types,         \
				 service_types_nr, log_file_name)    \
	struct c2_rpc_server_ctx name = {                            \
		.rsx_xprts            = (xprts),                     \
		.rsx_xprts_nr         = (xprts_nr),                  \
		.rsx_argv             = (server_argv),               \
		.rsx_argc             = (server_argc),               \
		.rsx_service_types    = (service_types),             \
		.rsx_service_types_nr = (service_types_nr),          \
		.rsx_log_file_name    = (log_file_name)              \
	}

#define C2_RPC_SERVER_CTX_DEFINE_SIMPLE(name, xprt_ptr, server_argv,  \
					log_file_name)                \
	C2_RPC_SERVER_CTX_DEFINE(name, &(xprt_ptr), 1, (server_argv), \
				 ARRAY_SIZE(server_argv),             \
				 c2_cs_default_stypes,                \
				 c2_cs_default_stypes_nr, (log_file_name))
#endif

struct c2_rpc_client_ctx;

/**
  A wrapper around c2_rpc_client_start(). It initializes dbenv and cob_domain
  within c2_rpc_client_ctx structure, and then calls c2_rpc_client_start().

  @param rctx  Initialized rpc context structure.
*/
int c2_rpc_client_init(struct c2_rpc_client_ctx *ctx);

/**
  A wrapper around c2_rpc_client_stop(). It finalizes dbenv and cob_domain
  within c2_rpc_client_ctx structure, and then calls c2_rpc_client_stop().

  @param rctx  Initialized rpc context structure.
*/
int c2_rpc_client_fini(struct c2_rpc_client_ctx *ctx);

#endif /* __COLIBRI_UT_RPC_H__ */

