#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "net/net.h"

// #define C2_RPC_CLIENT 1
#include "net/net_types.h"
#include "net/xdr.h"

#define CU_ASSERT(a)	if ((a)) {abort();}

bool test_op1_hanlder(struct c2_node_id *arg, struct c2_node_id *ret)
{
	int a;

	a = atoi((char *)&arg->uuid);
	sprintf(ret->uuid, "%d", a + a);

	return true;
}

enum test_ops {
	TEST_OP1 = 2000,
	TEST_OP2,
};


struct c2_rpc_op  test_rpc[] = {
	{ .ro_op = TEST_OP1,
	  .ro_arg_size = sizeof(struct c2_node_id),
	  .ro_xdr_arg = (c2_xdrproc_t)c2_xdr_node_id,
	  .ro_result_size = sizeof(struct c2_node_id),
	  .ro_xdr_result = (c2_xdrproc_t)c2_xdr_node_id,
	  .ro_handler = C2_RPC_SRV_PROC(test_op1_hanlder)
	 }
};

struct c2_rpc_op_table ops = {
	.rot_numops 	= ARRAY_SIZE(test_rpc),
	.rot_ops	= test_rpc
};

int main(int argc, char *argv[])
{
	int rc;
	struct c2_node_id node1 = { .uuid = "node-1" };
	struct c2_net_conn *conn1;
	struct c2_node_id  node_arg = { .uuid = {0} };
	struct c2_node_id  node_ret = { .uuid = {0} };
	struct c2_service s;

	rc = net_init();
	CU_ASSERT(rc);

	rc = c2_net_service_start(C2_SESSION_PROGRAM, 1, &ops, &s);
	CU_ASSERT(rc < 0);
	

	sleep(1);
	/* in config*/
	rc = c2_net_conn_create(&node1, C2_SESSION_PROGRAM, "localhost");
	CU_ASSERT(rc);

	conn1 = c2_net_conn_find(&node1);
	CU_ASSERT(conn1 == NULL);

	sprintf(node_arg.uuid, "%d", 10);
	rc = c2_net_cli_call_sync(conn1,
			 &ops, TEST_OP1, &node_arg, &node_ret);

	printf("rc = %d\n", rc);
	printf("%s\n", node_ret.uuid);
	CU_ASSERT(rc != 0);

	c2_net_conn_unlink(conn1);
	c2_net_conn_release(conn1);

	c2_net_service_stop(&s);

	net_fini();
	return 0;
}
