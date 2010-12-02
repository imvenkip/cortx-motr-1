/* -*- C -*- */
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
 * Domain code.
 */

/**
  Finalize a domain.

  @param dom the domain to be finalized.
*/
static void ksunrpc_dom_fini(struct c2_net_domain *dom)
{
	struct ksunrpc_dom *xdom;

	c2_list_fini(&dom->nd_conn);
	c2_list_fini(&dom->nd_service);

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

static const struct c2_net_xprt_ops ksunrpc_xprt_ops = {
	.xo_dom_init        = ksunrpc_dom_init,
	.xo_dom_fini        = ksunrpc_dom_fini,
	.xo_service_id_init = ksunrpc_service_id_init,
	.xo_service_init    = NULL
};

struct c2_net_xprt c2_net_ksunrpc_xprt = {
	.nx_name = "sunrpc/linux_kernel",
	.nx_ops  = &ksunrpc_xprt_ops
};
C2_EXPORTED(c2_net_ksunrpc_xprt);

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
