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
 * Original author Maxim Medved <max_medved@xyratex.com>
 * Original creation date: 03/22/2012
 */

#ifndef __NET_TEST_SERVER_H__
#define __NET_TEST_SERVER_H__

/**
   @defgroup NetTestServerDFS Test Server
   @ingroup NetTestDFS

   @see
   @ref net-test

   @{
 */

struct c2_net_test_server_cfg {
};

struct c2_net_test_server_ctx {
};

/**
   Initialize server data structures.
   @see @ref net-test-lspec
 */
int c2_net_test_server_init(struct c2_net_test_server_ctx *ctx,
			    struct c2_net_test_server_cfg *cfg);

/**
   Finalize server data structures.
   @see @ref net-test-lspec
 */
void c2_net_test_server_fini(struct c2_net_test_server_ctx *ctx);

/**
   Start test server.
   This function will return only after test server finished or interrupted
   with c2_net_test_server_stop().
   @see @ref net-test-lspec
 */
int c2_net_test_server_start(struct c2_net_test_server_ctx *ctx);

/**
   Stop test server.
   @see @ref net-test-lspec
 */
void c2_net_test_server_stop(struct c2_net_test_server_ctx *ctx);


/**
   @} end of NetTestServerDFS group
 */

#endif /*  __NET_TEST_SERVER_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
