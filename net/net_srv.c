#include "lib/cdefs.h"
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/pmap_clnt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <rpc/auth.h>
#include <rpc/svc.h>

#include <stdlib.h> /* exit */

#include "net/net.h"
#include "net/net_internal.h"

void c2_net_srv_fn_generic(struct svc_req *req, struct SVCXPRT *transp,
			   struct c2_rpc_op_table const *ops, void *arg, void *ret)
{
	bool retval;
	struct c2_rpc_op const *op;

	op = c2_find_op(ops, req->rq_proc);
	if (op == NULL) {
		svcerr_noproc ((SVCXPRT *)transp);
		return;
	}

	if (!svc_getargs((SVCXPRT *)transp, (xdrproc_t) op->ro_xdr_arg,
			 (caddr_t) arg)) {
		svcerr_decode ((SVCXPRT *)transp);
		return;
	}

	/** XXX need auth code */
	retval = (*op->ro_shandler)(arg, ret);
	if (retval && !svc_sendreply((SVCXPRT *)transp,
				     (xdrproc_t) op->ro_xdr_result,
				     ret)) {
		svcerr_systemerr ((SVCXPRT *)transp);
	}

	if (!svc_freeargs((SVCXPRT *)transp, (xdrproc_t) op->ro_xdr_arg,
			  (caddr_t) arg)) {
		/* bug */
	}

	xdr_free ((xdrproc_t) op->ro_xdr_result, (caddr_t) ret);
}

int c2_net_srv_start(unsigned long int programm, unsigned long ver, rpc_handler_t handler)
{
	SVCXPRT *transp;

	pmap_unset (programm, ver);

	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (transp == NULL) {
		exit(1);
	}
	if (!svc_register(transp, programm, ver, handler, IPPROTO_TCP)) {
		exit(1);
	}

	svc_run ();
	exit (1);
	/* NOTREACHED */
}

int c2_net_srv_stop(unsigned long int program_num, unsigned long ver)
{

	return 0;
}

