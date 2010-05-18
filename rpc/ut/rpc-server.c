#include "net/net.h"
#include "net/net_types.h"

#include "rpc/rpclib.h"
#include "rpc/rpc_ops.h"
#include "rpc/pcache.h"


int main(void)
{
	struct c2_service_id srv_id = { .uuid = "srv-1" };
	struct c2_rpc_server *srv;
	struct c2_service	s;
	int rc;

	c2_rpclib_init();

	rc = c2_net_service_start(C2_SESSION_PROGRAM, C2_DEF_RPC_VER,
				  C2_DEF_RPC_PORT, 1, rpc_ops, &s);

	srv = c2_rpc_server_create(&srv_id);

	/* init storage if need */

	/* service want pcache support */
	c2_pcache_init(srv);

	/* service want session support */
	/* need add commands to service */
	// c2_server_session_init(srv);

	/* add compound rpc in list*/
	// c2_server_compond_init(srv);

	/* answer to reply */
	c2_rpc_server_register(srv);

	while(1) {};
}
