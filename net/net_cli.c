#include <memory.h>
#include <rpc/clnt.h>

#include "net/net.h"
#include "net/net_internal.h"

/* XXX Default timeout  - need to be move in connection */
static struct timeval TIMEOUT = { 25, 0 };

int c2_net_cli_call_sync(struct c2_net_conn const *conn,
			 struct c2_rpc_op_table const *rot,
			 int op, void *arg, void *ret)
{
	struct c2_rpc_op const *rop;

	rop = c2_find_ops(rot, op);
	if (rop == NULL)
		return -ENODEV;

	return clnt_call(conn->nc_cli, op,
			 (xdrproc_t) rop->ro_xdr_arg, (caddr_t) arg,
			 (xdrproc_t) rop->ro_xdr_result, (caddr_t) ret,
			 TIMEOUT);
}


int c2_net_cli_call_async(struct c2_net_conn const *conn,
			  struct c2_rpc_op_table const *rot,
			  int op, void *arg,
			  c2_net_cli_cb *cb, void *ret)
{
	struct c2_rpc_op const *rop;
	int32_t err;

	rop = c2_find_ops(rot, op);
	if (rop == NULL)
		return -ENODEV;

	/** XXX until real async exist */
	err = clnt_call((CLIENT *) conn->nc_cli, op,
			(xdrproc_t) rop->ro_xdr_arg, (caddr_t) arg,
			(xdrproc_t) rop->ro_xdr_result, (caddr_t) ret,
			TIMEOUT);

	cb(err, arg, ret);

	return 0;
}
