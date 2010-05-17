#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/pmap_clnt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <rpc/auth.h>
#include <rpc/svc.h>

#include <unistd.h> /* fork */
#include <signal.h> /* kill */

#include <errno.h>
#include <stdlib.h> /* exit */

#include "lib/cdefs.h"
#include "lib/memory.h"
#include "net/net.h"

static struct c2_rpc_op_table *ops;

void c2_net_srv_fn_generic(struct svc_req *req, SVCXPRT *transp)
{
	bool retval;
	const struct c2_rpc_op *op;
	void *arg;
	void *ret;

	op = c2_rpc_op_find(ops, req->rq_proc);
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
	c2_free(ret);
out:
	c2_free(arg);
}

int c2_net_service_start(enum c2_rpc_service_id id, struct c2_rpc_op_table *o,
			 struct c2_service *service)
{
	SVCXPRT *transp;
	pid_t pid;

	pid = fork();
	if (pid < 0)
		return -errno;

	if (pid != 0) {
		/* service->s_child = pid; */
		return 0;
	}

	ops = o;

	pmap_unset (id, C2_DEF_RPC_VER);

	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (transp == NULL)
		return -ENOTSUP;

	if (!svc_register(transp, id, C2_DEF_RPC_VER,
			  c2_net_srv_fn_generic, IPPROTO_TCP)) {
		return -ENOTSUP;
	}

	svc_run ();

	return 0;
}

int c2_net_service_stop(struct c2_service *service)
{

	/* kill(service->s_child, 9); */
	return 0;
}

