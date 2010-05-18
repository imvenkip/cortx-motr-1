#include "net/net.h"

#include "rpc/rpclib.h"

int main(int argc, char *argv[])
{
	const c2_service_id srv_id = { .uuid = "srv-1"; }

	c2_rpclib_init();

	/* in config*/
	rc = c2_net_conn_create(&srv_id, C2_SESSION_PROGRAM, "localhost");
	CU_ASSERT(rc);

	cli = c2_rpc_client_init(&srv_id);

	c2_rpc_client_unlink(cli);
}