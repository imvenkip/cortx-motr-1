#include <net/net.h>
#include <rpc/rpclib.h>

int main(int argc, char *argv[])
{
	struct c2_service_id srv_id = { .si_uuid = "srv-1" };
	struct c2_rpc_client *cli;
	int rc;

	c2_rpclib_init();

	/* in config*/
	rc = c2_net_conn_create(&srv_id);

	cli = c2_rpc_client_create(&srv_id);


	c2_rpc_client_unlink(cli);

	return 0;
}
