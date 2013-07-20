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
 * Original creation date: 11/26/2012
 */

#include "net/test/network.h"		/* m0_net_test_network_init */
#include "net/test/node_bulk.h"		/* m0_net_test_node_bulk_init */
#include "net/test/initfini.h"

/**
   @defgroup NetTestInitFiniInternals Initialization/finalization of net-test
   @ingroup NetTestInternals

   @see
   @ref net-test

   @{
 */

struct init_fini {
	int  (*if_init)(void);
	void (*if_fini)(void);
};

static const struct init_fini if_list[] = {
#define NET_TEST_MODULE(name) {			  \
	.if_init = m0_net_test_ ## name ## _init, \
	.if_fini = m0_net_test_ ## name ## _fini, \
}
	NET_TEST_MODULE(network),
	NET_TEST_MODULE(node_bulk),
#undef NET_TEST_MODULE
};

static int net_test_initfini(bool init)
{
	size_t i = ARRAY_SIZE(if_list);
	int    rc = 0;

	if (init) {
		for (i = 0; i < ARRAY_SIZE(if_list); ++i) {
			rc = if_list[i].if_init();
			if (rc != 0) {
				init = false;
				break;
			}
		}
	}
	if (!init) {
		for (; i != 0; --i)
			if_list[i - 1].if_fini();
	}
	return rc;
}

int m0_net_test_init(void)
{
	return net_test_initfini(true);
}

void m0_net_test_fini(void)
{
	int rc = net_test_initfini(false);
	M0_POST(rc == 0);
}

/** @} end of NetTestInitFiniInternals group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
