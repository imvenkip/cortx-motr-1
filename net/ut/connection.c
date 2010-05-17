#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/net.h"
#include "net/net_types.h"

#define CU_ASSERT(a)	if ((a)) {abort();}

/* The suite initialization function.
 * Returns zero on success, non-zero otherwise.
 */
static
int init_suite(void)
{
	int rc;

	rc = net_init();
	return rc;
}

/* The suite cleanup function.
 * Returns zero on success, non-zero otherwise.
 */
static
int clean_suite(void)
{
	net_fini();
	return 0;
}

static
void test_create(void)
{
	int rc;
	struct c2_node_id node1 = { .uuid = "node-1" };

	rc = c2_net_conn_create(&node1, 0x20000001, C2_DEF_RPC_VER,
				"localhost", C2_DEF_RPC_PORT);
	CU_ASSERT( rc != 0);
}

static
void test_destroy(void)
{
	struct c2_node_id node1 = { .uuid = "node-1" };
	struct c2_net_conn *conn1, *conn2;

	conn1 = c2_net_conn_find(&node1);
	CU_ASSERT(conn1 == NULL);
	c2_net_conn_unlink(conn1);
	conn2 = c2_net_conn_find(&node1);
	CU_ASSERT(conn2 != NULL);
	c2_net_conn_release(conn1);
}

static
void test_create2(void)
{
	struct c2_node_id node1 = { .uuid = "node-1" };
	int rc;

	rc = c2_net_conn_create(&node1, 0x20000001, C2_DEF_RPC_VER,
				"localhost", C2_DEF_RPC_PORT);
	CU_ASSERT( rc != 0);
//	rc = c2_net_conn_create(&node1, 0x20000001, C2_DEF_RPC_VER,
//				"localhost", C2_DEF_RPC_PORT);
//	CU_ASSERT(rc == 0);
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
