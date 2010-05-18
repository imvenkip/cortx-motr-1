/* -*- C -*- */
#include <errno.h>

#include "rpc/rpclib.h"
#include "rpc/session_srv.h"

int c2_server_session_init(struct c2_rpc_server *srv)
{
	int rc;

	rc = c2_cache_init(&srv->rs_sessions);
	if (rc < 0)
		goto out;
}

void c2_server_session_fini(struct c2_rpc_server *srv)
{
	c2_cache_fini(&srv->rs_session);
}


/*** ***/
int c2_srv_session_init(struct c2_rpc_server *srv, uint32_t highslot,
			struct c2_service_id *cli_id;
			struct c2_srv_session **sess)
{
}

void c2_srv_session_unlink(struct c2_rpc_server *srv, struct c2_srv_session *sess)
{
}

struct c2_srv_session *c2_srv_session_find_by_id(struct c2_rpc_server *srv,
						 const c2_session_id *ss_id)
{
}
