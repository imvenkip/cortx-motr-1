#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <errno.h>

#include "net/net.h"
#include "net/net_types.h"
#include "net/connection.h"

/* XXX Default timeout  - need to be move in connection */
static struct timeval TIMEOUT = { 25, 0 };

int c2_net_cli_call_sync(const struct c2_net_conn *conn,
			 struct c2_rpc_op_table *rot,
			 int op, void *arg, void *ret)
{
	struct c2_rpc_op const *rop;

	rop = c2_rpc_op_find(rot, op);
	if (rop == NULL)
		return -EOPNOTSUPP;

	return clnt_call(conn->nc_cli, op,
			 (xdrproc_t) rop->ro_xdr_arg, (caddr_t) arg,
			 (xdrproc_t) rop->ro_xdr_result, (caddr_t) ret,
			 TIMEOUT);
}


int c2_net_cli_call_async(const struct c2_net_conn *conn,
			  struct c2_rpc_op_table *rot,
			  int op, void *arg,
			  c2_net_cli_cb cb, void *ret)
{
	struct c2_rpc_op const *rop;
	int32_t err;

	rop = c2_rpc_op_find(rot, op);
	if (rop == NULL)
		return -EOPNOTSUPP;

	/** XXX until real async exist */
	err = clnt_call(conn->nc_cli, op,
			(xdrproc_t) rop->ro_xdr_arg, (caddr_t) arg,
			(xdrproc_t) rop->ro_xdr_result, (caddr_t) ret,
			TIMEOUT);

	cb(err, arg, ret);

	return 0;
}
