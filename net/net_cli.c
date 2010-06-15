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

int c2_net_cli_call(struct c2_net_conn *conn,
		    struct c2_rpc_op_table *rot,
		    uint64_t op, void *arg, void *ret)
{
	const struct c2_rpc_op *rop;
	int                     result;

	rop = c2_rpc_op_find(rot, op);
	if (rop != NULL)
		result = conn->nc_ops->sio_call(conn, rop, arg, ret);
	else
		result = -EOPNOTSUPP;
	return result;
}


int c2_net_cli_send(struct c2_net_conn *conn, struct c2_net_async_call *call)
{
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
