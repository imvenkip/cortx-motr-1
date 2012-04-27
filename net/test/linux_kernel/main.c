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
 * Original author: Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 4/10/2012
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "net/test/node_main.h"
#include "net/test/linux_kernel/node_config_k.h"

MODULE_AUTHOR("Xyratex International");
MODULE_DESCRIPTION("Colibri Network Benchmark Module");
MODULE_LICENSE("proprietary");

static struct c2_net_test_node_config node_config;

static int __init c2_net_test_module_init(void)
{
	int rc;

	rc = c2_net_test_node_config_init(&node_config);
	if (rc == 0)
		rc = c2_net_test_init(&node_config);

	return rc;
}

static void __exit c2_net_test_module_fini(void)
{
	c2_net_test_fini();
	c2_net_test_node_config_fini(&node_config);
}

module_init(c2_net_test_module_init)
module_exit(c2_net_test_module_fini)

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
