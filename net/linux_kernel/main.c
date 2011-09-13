/* -*- C -*- */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <linux/module.h>
#include <linux/kernel.h>

#include "net/net.h"
#include "net/bulk_emulation/mem_xprt.h"
#include "net/bulk_emulation/sunrpc_xprt.h"
#include "net/ksunrpc/ksunrpc.h"

/**
   @addtogroup net Networking

  @{
 */

static int __init c2_net_init_k(void)
{
	int rc;
	rc = c2_net_init();
	if (rc != 0)
		return rc;
	rc = c2_mem_xprt_init();
	if (rc != 0)
		return rc;
	rc = c2_sunrpc_fop_init();
	if (rc != 0)
		return rc;
	rc = c2_ksunrpc_init();
	if (rc != 0)
		return rc;
	printk(KERN_INFO "Colibri Kernel Messaging initialized");
	return 0;
}

static void __exit c2_net_fini_k(void)
{
	printk(KERN_INFO "Colibri Kernel Messaging removed\n");
	c2_sunrpc_fop_fini();
	c2_mem_xprt_fini();
	c2_mem_xprt_fini();
	c2_net_fini();
}

module_init(c2_net_init_k)
module_exit(c2_net_fini_k)

MODULE_AUTHOR("Xyratex");
MODULE_DESCRIPTION("Colibri Kernel Messaging");
/* GPL license required as long as kernel sunrpc is used */
MODULE_LICENSE("GPL");

/** @} end of group net */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
