#include "net/net.h"

#include "rpc/rpclib.h"
#include "rpc/pcache.h


int main(void)
{
	const struct c2_service_id srv_id = { .uuid = "srv-1"; }
	struct c2_rpc_server *srv;

	srv = c2_server_create(&srv_id, "/tmp");

	/* service want pcache support */
	c2_pcache_init(srv);

	/* service want session support */
	/* need add commands to service */
	c2_server_session_init(srv);

	/* add compound rpc in list*/
	// c2_server_compond_init(srv);

	c2_server_register(srv);
}
