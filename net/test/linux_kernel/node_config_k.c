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
static long  packet_count     = C2_NET_TEST_CONFIG_COUNT_DEFAULT;
static long  bulk_packet_size = C2_NET_TEST_CONFIG_SIZE_DEFAULT;
static char *console_address  = C2_NET_TEST_CONFIG_CONSOLE_DEFAULT;
static char *targets[C2_NET_TEST_CONFIG_TARGETS_MAX];
static int   targets_nr       = 0;

module_param_named(role, node_role, charp, S_IRUGO);
MODULE_PARM_DESC(role, "node role: can be client or server");

module_param_named(type, test_type, charp, S_IRUGO);
MODULE_PARM_DESC(type, "test type: can be ping or bulk");

module_param_named(count, packet_count, long, S_IRUGO);
MODULE_PARM_DESC(count,
		"number of packets which need to be send to test server");

module_param_named(size, bulk_packet_size, long, S_IRUGO);
MODULE_PARM_DESC(size, "bulk packet size");

module_param_named(console, console_address, charp, S_IRUGO);
MODULE_PARM_DESC(console, "console address");

module_param_array_named(target, targets, charp, &targets_nr, S_IRUGO);
MODULE_PARM_DESC(target, "test targets");

int c2_net_test_config_init(void)
{
	int i;

	/* XXX debug */
	printk(KERN_INFO "role    = %s\n", node_role);
	printk(KERN_INFO "type    = %s\n", test_type);
	printk(KERN_INFO "count   = %s\n", test_type);
	printk(KERN_INFO "size    = %s\n", test_type);
	printk(KERN_INFO "console = %s\n", test_type);
	printk(KERN_INFO "targets = ");

	for (i = 0; i < targets_nr; ++i)
		printk(KERN_INFO "%s, ", targets[i]);
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
