#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "net/net.h"
#include "net/sunrpc/sunrpc.h"

#define CU_ASSERT(a)	assert(a)

static struct c2_net_domain dom;
static struct c2_service_id sid;

/* The suite initialization function.
 * Returns zero on success, non-zero otherwise.
 */
static int init_suite(void)
{
	int rc;

	rc = c2_net_init();
	CU_ASSERT(rc == 0);

	rc = c2_net_xprt_init(&c2_net_sunrpc_xprt);
	CU_ASSERT(rc == 0);
	
	rc = c2_net_domain_init(&dom, &c2_net_sunrpc_xprt);
	CU_ASSERT(rc == 0);

	rc = c2_service_id_init(&sid, &dom, "127.0.0.1", 10001);
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
	c2_net_xprt_fini(&c2_net_sunrpc_xprt);
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



int main(int argc, char *argv[])
{
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
