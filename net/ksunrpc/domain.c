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
 * Original author: Huang Hua <Hua_Huang@xyratex.com>
 * Original creation date: 05/30/2010
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/smp_lock.h>
#include <linux/uaccess.h>
#include <linux/vfs.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/utsname.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xdr.h>


#include "lib/assert.h"
#include "lib/list.h"
#include "lib/memory.h"
#include "net/net.h"
#include "net/ksunrpc/ksunrpc.h"

/**
   @addtogroup ksunrpc Kernel Level Sun RPC
   @{
 */

/*
 * Maximum bulk IO size
 */
enum {
	KSUNRPC_MAX_BRW_SIZE = (4 << 20)
};

/*
 * Domain code.
 */

/**
  Finalize a domain.

  @param dom the domain to be finalized.
*/
static void ksunrpc_dom_fini(struct c2_net_domain *dom)
{
	struct ksunrpc_dom *xdom;

	C2_ASSERT(c2_list_is_empty(&dom->nd_conn));
	C2_ASSERT(c2_list_is_empty(&dom->nd_service));

	xdom = dom->nd_xprt_private;
	if (xdom != NULL) {
		c2_free(xdom);
	}
}

/**
  Init a kernel sunprc domain.
*/
static int ksunrpc_dom_init(struct c2_net_xprt *xprt, struct c2_net_domain *dom)
{
	struct ksunrpc_dom *xdom;
	int    result;

	C2_ALLOC_PTR(xdom);
	if (xdom != NULL) {
		dom->nd_xprt_private = xdom;
		xdom->kd_dummy = 0;
		result = 0;
	} else
		result = -ENOMEM;
	if (result != 0)
		ksunrpc_dom_fini(dom);
	return result;
}

static size_t ksunrpc_net_bulk_size(void)
{
	return KSUNRPC_MAX_BRW_SIZE;
}

static const struct c2_net_xprt_ops ksunrpc_xprt_ops = {
	.xo_dom_init        = ksunrpc_dom_init,
	.xo_dom_fini        = ksunrpc_dom_fini,
	.xo_service_id_init = ksunrpc_service_id_init,
	.xo_service_init    = ksunrpc_service_init,
	.xo_net_bulk_size   = ksunrpc_net_bulk_size
};

struct c2_net_xprt c2_net_ksunrpc_xprt = {
	.nx_name = "sunrpc/linux_kernel",
	.nx_ops  = &ksunrpc_xprt_ops
};

/**
   Minimal version of the ksunrpc transport with a total of 4 threads
   when run with a server.
 */
struct c2_net_xprt c2_net_ksunrpc_minimal_xprt = {
	.nx_name = "minimal-sunrpc/linux_kernel",
	.nx_ops  = &ksunrpc_xprt_ops
};

int c2_ksunrpc_init(void)
{
	return ksunrpc_server_init();
}

void c2_ksunrpc_fini(void)
{
	ksunrpc_server_fini();
}

/** @} end of group usunrpc */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
