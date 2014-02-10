/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 17-Jan-2014
 */
#pragma once
#ifndef __MERO_NET_MODULE_H__
#define __MERO_NET_MODULE_H__

#include "module/module.h"  /* m0_module */
#include "net/net.h"        /* m0_net_domain */

struct m0_net;

/**
 * @addtogroup net
 *
 * `net' layer defines two modules: m0_net and m0_net_xprt_module.
 * The levels of these modules are shown on the diagram:
 *
 * @verbatim
 *                         m0_net_xprt_module
 *                       +=====================+
 *   m0_net              | M0_LEVEL_NET_DEP    |
 * +==============+      +---------------------+
 * | M0_LEVEL_NET | <-~~ | M0_LEVEL_NET_XPRT   |
 * +--------------+      +---------------------+
 *                       | M0_LEVEL_NET_DOMAIN |
 *                       +---------------------+
 * @endverbatim
 *
 * @see module/module.h to get familiar with the concept of modules,
 * levels, and dependencies.
 *
 * @{
 */

/**
 * Performs initial configuration of `net' module and the modules
 * embedded in it.
 */
M0_INTERNAL void m0_net_modules_setup(struct m0_net *net);

/** Levels of m0_net::n_module. */
enum {
	/** m0_mero_init() has been called. */
	M0_LEVEL_NET = 1
};

/** Levels of m0_net_xprt_module::nx_module. */
enum {
	/** Dependency on (m0_net, M0_LEVEL_NET) is established. */
	M0_LEVEL_NET_DEP = 1,
	/** m0_net_xprt_module::nx_xprt is initialised. */
	M0_LEVEL_NET_XPRT,
	/** m0_net_xprt_module::nx_domain is initialised. */
	M0_LEVEL_NET_DOMAIN
};

/** Network transport module. */
struct m0_net_xprt_module {
	struct m0_module     nx_module;
	struct m0_net_xprt  *nx_xprt;
	struct m0_net_domain nx_domain;
};

/** Identifiers of network transports. */
enum m0_net_xprt_id {
	M0_NET_XPRT_LNET,
	M0_NET_XPRT_BULK_MEM,
	M0_NET_XPRT_NR
};

/** Network module. */
struct m0_net {
	struct m0_module          n_module;
	struct m0_net_xprt_module n_xprts[M0_NET_XPRT_NR];
};

/** @} net */
#endif /* __MERO_NET_MODULE_H__ */
