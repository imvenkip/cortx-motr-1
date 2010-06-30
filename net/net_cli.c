#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <errno.h>
#include "net.h"

/**
   @addtogroup net Networking.

   @{
 */

static const struct c2_addb_loc net_cli_addb = {
	.al_name = "net-cli"
};

C2_ADDB_EV_DEFINE(net_addb_conn_send, "send", 0x10, C2_ADDB_STAMP);
C2_ADDB_EV_DEFINE(net_addb_conn_call, "call", 0x11, C2_ADDB_STAMP);

#define ADDB_ADD(conn, ev, ...) \
C2_ADDB_ADD(&(conn)->nc_addb, &net_cli_addb, ev , ## __VA_ARGS__)

int c2_net_cli_call(struct c2_net_conn *conn, struct c2_net_call *call)
{
	ADDB_ADD(conn, net_addb_conn_call);
	return conn->nc_ops->sio_call(conn, call);
}


int c2_net_cli_send(struct c2_net_conn *conn, struct c2_net_call *call)
{
	ADDB_ADD(conn, net_addb_conn_send);
	return conn->nc_ops->sio_send(conn, call);
}

int c2_service_id_init(struct c2_service_id *id, struct c2_net_domain *dom, ...)
{
	va_list varargs;
	int     result;

	id->si_domain = dom;
	va_start(varargs, dom);
	result = dom->nd_xprt->nx_ops->xo_service_id_init(id, varargs);
	va_end(varargs);
	return result;
}

void c2_service_id_fini(struct c2_service_id *id)
{
	id->si_ops->sis_fini(id);
}

/** @} end of net group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
