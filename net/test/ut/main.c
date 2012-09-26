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

#include "lib/ut.h"		/* c2_test_suite */

#include "net/test/network.h"	/* c2_net_test_network_init */

extern void c2_net_test_ringbuf_ut(void);

extern void c2_net_test_serialize_ut(void);

extern void c2_net_test_str_ut(void);

extern void c2_net_test_slist_ut(void);

extern void c2_net_test_network_ut_buf_desc(void);
extern void c2_net_test_network_ut_ping(void);
extern void c2_net_test_network_ut_bulk(void);

extern void c2_net_test_cmd_ut_single(void);
extern void c2_net_test_cmd_ut_multiple(void);

extern void c2_net_test_stats_ut(void);
extern void c2_net_test_timestamp_ut(void);

extern void c2_net_test_service_ut(void);

extern void c2_net_test_client_server_ping_ut(void);
extern void c2_net_test_client_server_bulk_ut(void);

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
		{ "ringbuf",		c2_net_test_ringbuf_ut		  },
		{ "serialize",		c2_net_test_serialize_ut	  },
		{ "str",		c2_net_test_str_ut		  },
		{ "slist",		c2_net_test_slist_ut		  },
		{ "network-buf-desc",	c2_net_test_network_ut_buf_desc	  },
		{ "network-ping",	c2_net_test_network_ut_ping	  },
		{ "network-bulk",	c2_net_test_network_ut_bulk	  },
		{ "cmd-single",		c2_net_test_cmd_ut_single	  },
		{ "cmd-multiple",	c2_net_test_cmd_ut_multiple	  },
		{ "stats",		c2_net_test_stats_ut		  },
		{ "timestamp",		c2_net_test_timestamp_ut	  },
		{ "service",		c2_net_test_service_ut		  },
		{ "client-server-ping",	c2_net_test_client_server_ping_ut },
		{ "client-server-bulk",	c2_net_test_client_server_bulk_ut },
		{ NULL,			NULL				  }
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
