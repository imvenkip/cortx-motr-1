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

/* @todo proper kernel module init */
/* node_config_k.h */
#if 0
#include "net/test/node_config.h"

/*
   Set m0_net_test_node_config structure according to kernel module parameters.
 */
int m0_net_test_node_config_init(struct m0_net_test_node_config *cfg);

/**
   Finalize m0_net_test_node_config structure (free memory etc.)
 */
void m0_net_test_node_config_fini(struct m0_net_test_node_config *cfg);
#endif

/* node_config_k.c */
#if 0
#include <linux/module.h>
#include <linux/kernel.h>

#include "net/test/linux_kernel/node_config_k.h"

static char *node_role	      = NULL;
static char *test_type	      = NULL;
static long  count	      = M0_NET_TEST_CONFIG_COUNT_DEFAULT;
static long  size	      = M0_NET_TEST_CONFIG_SIZE_DEFAULT;
static char *console	      = M0_NET_TEST_CONFIG_CONSOLE_DEFAULT;
static char *target[M0_NET_TEST_CONFIG_TARGETS_MAX];
static int   target_nr        = 0;

module_param(node_role, charp, S_IRUGO);
MODULE_PARM_DESC(node_role, "node role: can be client or server");

module_param(test_type, charp, S_IRUGO);
MODULE_PARM_DESC(test_type, "test type: can be ping or bulk");

module_param(count, long, S_IRUGO);
MODULE_PARM_DESC(count,
		"number of test messages which need to be send to test server");

module_param(size, long, S_IRUGO);
MODULE_PARM_DESC(size, "bulk test message size");

module_param(console, charp, S_IRUGO);
MODULE_PARM_DESC(console, "console address");

module_param_array(target, charp, &target_nr, S_IRUGO);
MODULE_PARM_DESC(target, "test targets");

int m0_net_test_node_config_init(struct m0_net_test_node_config *cfg)
{
	int i;

	/** @todo remove it. debug only */
	printk(KERN_INFO "node_role = %s\n",  node_role);
	printk(KERN_INFO "test_type = %s\n",  test_type);
	printk(KERN_INFO "count     = %ld\n", count);
	printk(KERN_INFO "size      = %ld\n", size);
	printk(KERN_INFO "console   = %s\n",  console);
	printk(KERN_INFO "target    = \n");

	for (i = 0; i < target_nr; ++i)
		printk(KERN_INFO "%s\n", target[i]);
	printk(KERN_INFO "end of target\n");

	/** @todo implement */

	return -ENOSYS;
}

void m0_net_test_node_config_fini(struct m0_net_test_node_config *cfg)
{
}
#endif

#if 0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include "net/test/node_main.h"
#include "net/test/linux_kernel/node_config_k.h"

MODULE_AUTHOR("Xyratex International");
MODULE_DESCRIPTION("Mero Network Benchmark Module");
MODULE_LICENSE("proprietary");

static struct m0_net_test_node_config node_config;

static int __init m0_net_test_module_init(void)
{
	int rc;

	rc = m0_net_test_node_config_init(&node_config);
	if (rc == 0)
		rc = m0_net_test_init(&node_config);

	return rc;
}

static void __exit m0_net_test_module_fini(void)
{
	m0_net_test_fini();
	m0_net_test_node_config_fini(&node_config);
}

module_init(m0_net_test_module_init)
module_exit(m0_net_test_module_fini)
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
