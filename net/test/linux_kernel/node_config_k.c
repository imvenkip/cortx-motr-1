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
 * Original creation date: 03/22/2012
 */

#include "net/test/node_config.h"

static char *node_role	      = NULL;
static char *test_type	      = NULL;
static long  count	      = C2_NET_TEST_CONFIG_COUNT_DEFAULT;
static long  size	      = C2_NET_TEST_CONFIG_SIZE_DEFAULT;
static char *console	      = C2_NET_TEST_CONFIG_CONSOLE_DEFAULT;
static char *target[C2_NET_TEST_CONFIG_TARGETS_MAX];
static int   target_nr       = 0;

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

int c2_net_test_config_init(void)
{
	int i;

	/* XXX debug */
	printk(KERN_INFO "node_role = %s\n",  node_role);
	printk(KERN_INFO "test_type = %s\n",  test_type);
	printk(KERN_INFO "count	    = %ld\n", count);
	printk(KERN_INFO "size      = %ld\n", size);
	printk(KERN_INFO "console   = %s\n",  console);
	printk(KERN_INFO "target    = ");

	for (i = 0; i < target_nr; ++i)
		printk(KERN_INFO "%s, ", target[i]);
	printk(KERN_INFO "\n");

	/* XXX implement */

	return -1;
}

void c2_net_test_config_fini(void)
{
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
