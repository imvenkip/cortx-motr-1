#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>

// #define C2_RPC_CLIENT 1
#include "net/net.h"
#include "net/xdr.h"
#include "net/sunrpc/sunrpc.h"

#define CU_ASSERT(a)	assert(a)

static bool test_op1_hanlder(const struct c2_rpc_op *op, void *arg, void *ret)
{
	struct c2_service_id *iid = arg;
	struct c2_service_id *oid = ret;
	int a;

	a = atoi((char *)&iid->si_uuid);
	sprintf(oid->si_uuid, "%d", a + a);

	return true;
}


enum test_ops {
	TEST_OP1 = 2000,
	TEST_OP2,
};


static struct c2_rpc_op  test_rpc1 = {
	.ro_op = TEST_OP1,
	.ro_arg_size = sizeof(struct c2_service_id),
	.ro_xdr_arg = (c2_xdrproc_t)c2_xdr_service_id,
	.ro_result_size = sizeof(struct c2_service_id),
	.ro_xdr_result = (c2_xdrproc_t)c2_xdr_service_id,
	.ro_handler = test_op1_hanlder
};

static struct c2_rpc_op  test_rpc2 = {
	.ro_op = TEST_OP2,
	.ro_arg_size = sizeof(struct c2_service_id),
	.ro_xdr_arg = (c2_xdrproc_t)c2_xdr_service_id,
	.ro_result_size = sizeof(struct c2_service_id),
	.ro_xdr_result = (c2_xdrproc_t)c2_xdr_service_id,
	.ro_handler = test_op1_hanlder
};

static struct c2_net_domain dom;

enum {
	PORT = 10001
};

int main(int argc, char *argv[])
{
	int rc;

	struct c2_service_id sid1 = { .si_uuid = "node-1" };
	struct c2_net_conn *conn1;
	struct c2_service_id  node_arg = { .si_uuid = {0} };
	struct c2_service_id  node_ret = { .si_uuid = {0} };
	struct c2_rpc_op_table *ops;
	struct c2_service s1;

	memset(&s1, 0, sizeof s1);

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	rc = c2_net_init();
	CU_ASSERT(rc == 0);

	rc = c2_net_xprt_init(&c2_net_sunrpc_xprt);
	CU_ASSERT(rc == 0);

	rc = c2_net_domain_init(&dom, &c2_net_sunrpc_xprt);
	CU_ASSERT(rc == 0);

	rc = c2_service_id_init(&sid1, &dom, "127.0.0.1", PORT);
	CU_ASSERT(rc == 0);

	c2_rpc_op_table_init(&ops);
	CU_ASSERT(ops != NULL);

	rc = c2_rpc_op_register(ops, &test_rpc1);

	rc = c2_rpc_op_register(ops, &test_rpc2);

	rc = c2_service_start(&s1, &sid1, ops);
	CU_ASSERT(rc >= 0);

	sleep(1);

	/* in config */
	rc = c2_net_conn_create(&sid1);
	CU_ASSERT(rc == 0);

	conn1 = c2_net_conn_find(&sid1);
	CU_ASSERT(conn1 != NULL);

	sprintf(node_arg.si_uuid, "%d", 10);
	rc = c2_net_cli_call(conn1, ops, TEST_OP1, &node_arg, &node_ret);

	printf("rc = %d\n", rc);
	printf("%s\n", node_ret.si_uuid);
	CU_ASSERT(rc == 0);

	c2_net_conn_unlink(conn1);
	c2_net_conn_release(conn1);

	c2_service_stop(&s1);
	c2_rpc_op_table_fini(ops);

	c2_service_id_fini(&sid1);
	c2_net_domain_fini(&dom);
	c2_net_xprt_fini(&c2_net_sunrpc_xprt);
	c2_net_fini();
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
