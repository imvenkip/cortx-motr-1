/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 01-Dec-2012
 */
#pragma once
#ifndef __COLIBRI_CONF_UT_RPC_HELPERS_H__
#define __COLIBRI_CONF_UT_RPC_HELPERS_H__

struct c2_net_xprt;
struct c2_rpc_machine;

/** Initialises net and rpc layers, performs c2_rpc_machine_init(). */
C2_INTERNAL int c2_ut_rpc_machine_start(struct c2_rpc_machine *mach,
					struct c2_net_xprt *xprt,
					const char *ep_addr,
					const char *dbname);

/** Performs c2_rpc_machine_fini(), finalises rpc and net layers. */
C2_INTERNAL void c2_ut_rpc_machine_stop(struct c2_rpc_machine *mach);

#endif /* __COLIBRI_CONF_UT_RPC_HELPERS_H__ */
