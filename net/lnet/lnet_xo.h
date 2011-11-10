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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 11/10/2011
 *
 */
#ifndef __COLIBRI_LNET_XO_H__
#define __COLIBRI_LNET_XO_H__

#include "net/lnet/lnet_core.h"

/**
   @defgroup LNetIDFS LNet Transport Internal Interface
   @ingroup LNetDFS
   @{
*/

struct lnet_xo_domain;
struct lnet_xo_transfer_mc;
struct lnet_xo_buffer;

/**
   Private data pointed to by c2_net_domain::nd_xprt_private.
 */
struct lnet_xo_domain {
	/** Pointer back to the network dom */
	struct c2_net_domain       *lxd_dom;

	/** LNet Core transfer domain data (shared memory) */
	struct c2_lnet_core_domain  lxd_core;
};

/**
   Private data pointed to by c2_net_transfer_mc::ntm_xprt_private.
 */
struct lnet_xo_transfer_mc {
	/** Pointer back to the network tm */
	struct c2_net_transfer_mc       *lxtm_tm;

	/** LNet Core transfer machine data (shared memory) */
	struct c2_lnet_core_transfer_mc  lxtm_core;
};

/**
   Private data pointed to by c2_net_buffer::nb_xprt_private.
 */
struct lnet_xo_buffer {
	/** Pointer back to the network buffer */
	struct c2_net_buffer       *lxb_nb;

	/** LNet Core buffer data (shared memory) */
	struct c2_lnet_core_buffer  lxb_core;
};


/**
   @} LNetIDFS
*/

#endif /* __COLIBRI_LNET_XO_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
