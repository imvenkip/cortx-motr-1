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

#ifndef __NET_TEST_CLIENT_H__
#define __NET_TEST_CLIENT_H__

/**
   @defgroup NetTestClientDFS Test Client
   @ingroup NetTestDFS

   @see
   @ref net-test

   @{
 */

/**
   Initialize client data structures.
   @see @ref net-test-lspec
 */
int c2_net_test_client_init(void);

/**
   Finalize client data structures.
   @see @ref net-test-lspec
 */
void c2_net_test_client_fini(void);

/**
   Start test client.
   This function will return only after test client finished or interrupted
   with c2_net_test_client_stop().
   @see @ref net-test-lspec
 */
int c2_net_test_client_start(void);

/**
   Stop test client.
   @see @ref net-test-lspec
 */
void c2_net_test_client_stop(void);

/**
   @} end of NetTestClientDFS group
 */

#endif /*  __NET_TEST_CLIENT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */

