#include <memory.h>
#include <rpc/clnt.h>

#include "net/net.h"
#include "net/net_internal.h"

/* Default timeout */
static struct timeval TIMEOUT = { 25, 0 };

int c2_net_cli_call_sync(struct c2_net_conn *conn, struct c2_rpc_op_table *rot,
			 int op, void *arg, void *ret)
{
	struct c2_rpc_op *rop;

	if ((rop = find_ops(rot, op)) == NULL) {
		return -ENODEV;
	}

	return clnt_call(conn->nc_cli, op,
			 rop->ro_xdr_arg, (caddr_t) arg,
			 rop->ro_xdr_result, (caddr_t) ret,
		TIMEOUT);
}


int c2_net_cli_call_async(struct c2_net_conn *conn, int op, void *arg,
			  c2_net_cli_cb *cb, void *ret)
{
	struct c2_rpc_op *rop;
	int32_t errno;

	if ((rop = find_ops(rot, op)) == NULL) {
		return -ENODEV;
	}

	/** XXX until real async exist */
	errno = clnt_call(conn->nc_cli, op,
			 rop->ro_xdr_arg, (caddr_t) arg,
			 rop->ro_xdr_result, (caddr_t) ret,
		TIMEOUT);

	cb(errno, arg, ret);

	return 0;
}
