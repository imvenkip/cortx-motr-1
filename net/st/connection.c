/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Alexey Lyashkov
 * Original creation date: 05/07/2010
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/assert.h"
#include "net/net.h"
#include "net/usunrpc/usunrpc.h"

#define CU_ASSERT(a)	C2_ASSERT(a)

static struct c2_net_domain dom;
static struct c2_service_id sid;
static char *test_addr;
static int test_port;

/* The suite initialization function.
 * Returns zero on success, non-zero otherwise.
 */
static int init_suite(void)
{
	int rc;

	rc = c2_net_init();
	CU_ASSERT(rc == 0);

	rc = c2_net_xprt_init(&c2_net_usunrpc_xprt);
	CU_ASSERT(rc == 0);
	
	rc = c2_net_domain_init(&dom, &c2_net_usunrpc_xprt);
	CU_ASSERT(rc == 0);

	rc = c2_service_id_init(&sid, &dom, test_addr, test_port);
	CU_ASSERT(rc == 0);

	return 0;
}

/* The suite cleanup function.
 * Returns zero on success, non-zero otherwise.
 */
static int clean_suite(void)
{
	c2_service_id_fini(&sid);
	c2_net_domain_fini(&dom);
	c2_net_xprt_fini(&c2_net_usunrpc_xprt);
	c2_net_fini();
	return 0;
}

static void test_create(void)
{
	int rc;

	rc = c2_net_conn_create(&sid);
	CU_ASSERT(rc == 0);
}

static void test_destroy(void)
{
	struct c2_net_conn *conn1;
	struct c2_net_conn *conn2;

	conn1 = c2_net_conn_find(&sid);
	CU_ASSERT(conn1 != NULL);
	c2_net_conn_unlink(conn1);
	conn2 = c2_net_conn_find(&sid);
	CU_ASSERT(conn2 == NULL);
	c2_net_conn_release(conn1);
}

static void test_create2(void)
{
	int rc;

	rc = c2_net_conn_create(&sid);
	CU_ASSERT(rc == 0);
}

int usage(char *appname)
{
	printf("%s ip-address port\nex: %s %s %s",
	       appname, appname, "127.0.0.1", "10001");
	return 1;
}

int main(int argc, char *argv[])
{
	if (argc != 3)
		return usage(argv[0]);
	test_addr = argv[1];
	test_port = atoi(argv[2]);

	init_suite();

	test_create();
	test_destroy();

	test_create2();
	test_destroy();

	clean_suite();
	return 0;
}

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
