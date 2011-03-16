#include "lib/errno.h"
#include "net/net.h"
#include "lib/memory.h"

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

/**
   Send the request to connection and wait for reply synchronously.

   The ->sio_call() is responsible to add the addb record and free it.
 */
int c2_net_cli_call(struct c2_net_conn *conn, struct c2_net_call *call)
{
	ADDB_ADD(conn, net_addb_conn_call);
	return conn->nc_ops->sio_call(conn, call);
}
C2_EXPORTED(c2_net_cli_call);

/**
   Send the request to connection asynchronously and don't wait for reply.

   The ->sio_send() is responsible to add the addb record and free it.
 */
int c2_net_cli_send(struct c2_net_conn *conn, struct c2_net_call *call)
{
	ADDB_ADD(conn, net_addb_conn_send);
	return conn->nc_ops->sio_send(conn, call);
}
C2_EXPORTED(c2_net_cli_send);

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
C2_EXPORTED(c2_service_id_init);

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
