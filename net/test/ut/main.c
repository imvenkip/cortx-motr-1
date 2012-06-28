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
 * Original creation date: 05/19/2012
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/ut.h"		/* c2_test_suite */

#include "net/test/network.h"	/* c2_net_test_network_init */

extern void c2_net_test_network_ut_buf_desc(void);
extern void c2_net_test_network_ut_ping(void);
extern void c2_net_test_network_ut_bulk(void);
extern void c2_net_test_network_ut_debug(void);

extern void c2_net_test_cmd_ut_single(void);
extern void c2_net_test_cmd_ut_multiple(void);

extern void c2_net_test_slist_ut(void);

static int net_test_init(void)
{

	return c2_net_test_network_init();
}

static int net_test_fini(void)
{
	c2_net_test_network_fini();
	return 0;
}

const struct c2_test_suite c2_net_test_ut = {
	.ts_name = "net-test",
	.ts_init = net_test_init,
	.ts_fini = net_test_fini,
	.ts_tests = {
		{ "network-buf-desc",	c2_net_test_network_ut_buf_desc	 },
		{ "network-ping",	c2_net_test_network_ut_ping	 },
		{ "network-bulk",	c2_net_test_network_ut_bulk	 },
		{ "network-debug",	c2_net_test_network_ut_debug	 },
		{ "slist",		c2_net_test_slist_ut		 },
		{ "cmd-single",		c2_net_test_cmd_ut_single	 },
		{ "cmd-multiple",	c2_net_test_cmd_ut_multiple	 },
		{ NULL,			NULL				 }
	}
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
