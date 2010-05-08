#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/pmap_clnt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <rpc/auth.h>
#include <rpc/svc.h>

#include <errno.h>
#include <stdlib.h> /* exit */

#include "lib/cdefs.h"
#include "lib/memory.h"
#include "net/net.h"
#include "net/net_types.h"

static struct c2_rpc_op_table *ops;

int c2_net_srv_ops_register(struct c2_rpc_op_table *o)
{
	ops = o;

	return 0;
}


void c2_net_srv_fn_generic(struct svc_req *req, SVCXPRT *transp)
{
	bool retval;
	const struct c2_rpc_op *op;
	void *arg;
	void *ret;

	op = c2_find_op(ops, req->rq_proc);
	if (op == NULL) {
		svcerr_noproc(transp);
		return;
	}

	arg = c2_alloc(op->ro_arg_size);
	if (!arg) {
		svcerr_systemerr(transp);
		return;
	}

	if (!svc_getargs(transp, (xdrproc_t) op->ro_xdr_arg,
			 (caddr_t) arg)) {
		svcerr_decode(transp);
		goto out;
	}

	ret  = c2_alloc(op->ro_result_size);
	if (!ret) {
		svcerr_systemerr(transp);
		goto out;
	}

	/** XXX need auth code */
	retval = (*op->ro_handler)(arg, ret);
	if (retval && !svc_sendreply(transp,
				     (xdrproc_t) op->ro_xdr_result,
				     ret)) {
		svcerr_systemerr(transp);
	}

	if (!svc_freeargs(transp, (xdrproc_t) op->ro_xdr_arg,
			  (caddr_t) arg)) {
		/* bug */
	}

	xdr_free ((xdrproc_t) op->ro_xdr_result, (caddr_t) ret);
	c2_free(ret, op->ro_result_size);
out:
	c2_free(arg, op->ro_arg_size);
}

int c2_net_srv_start(unsigned long int programm)
{
	SVCXPRT *transp;

	if (!ops)
		return -EINVAL;

	pmap_unset (programm, C2_DEF_RPC_VER);

	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (transp == NULL) {
		exit(1);
	}
	if (!svc_register(transp, programm, C2_DEF_RPC_VER,
			  c2_net_srv_fn_generic, IPPROTO_TCP)) {
		exit(1);
	}

	svc_run ();

	return 0;
}

int c2_net_srv_stop(unsigned long int program_num)
{

	return 0;
}

