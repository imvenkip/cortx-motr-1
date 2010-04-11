#include "lib/cdefs.h"

static int find_ops_idx(int nops, struct rpc_op *ops, int op)
{
	int i;

	for(i = 0; i < nops; i++)
		if (ops[i].ro_op == op)
			return i;
	return -1;
}

void c2_net_srv_fn_generic(struct svc_req *req, SVCXPRT *xptr,
			   int nops, struct rpc_op *ops, void *arg, void *ret)
{
	int idx;
	bool_t retval;
	xdrproc_t xdr_argument, xdr_result;
	bool (*local)(void *, void *);

	if ((rqstp->rq_proc > nops) || 
	    ((idx = find_ops_idx(nops, ops, rqstp->rq_proc)) == -1)) {
		svcerr_noproc (transp);
		return;
	}
	

	xdr_argument = ops[idx].xdr_arg;
	xdr_result = ops[idx].xdr_result;
	local = ops[idx].handler;

	if (!svc_getargs (transp, xdr_argument, (caddr_t) arg)) {
		svcerr_decode (transp);
		return;
	}

	/** XXX need auth code */
	retval = (*local)(arg, ret);
	if (retval && !svc_sendreply(transp, xdr_result, ret)) {
		svcerr_systemerr (transp);
	}

	if (!svc_freeargs (transp, xdr_argument, (caddr_t) arg)) {
		/* bug */
	}

	/* XXX check result */
	c2_session_program_1_freeresult (transp, _xdr_result, (caddr_t) &result))

}

int c2_net_srv_start(unsigned long int program_num, unsigned long ver, rpc_handler_t handler)
{
	register SVCXPRT *transp;

	pmap_unset (programm_num, ver);

	transp = svcudp_create(RPC_ANYSOCK);
	if (transp == NULL) {
		fprintf (stderr, "%s", "cannot create udp service.");
		exit(1);
	}
	if (!svc_register(transp, programm, ver, handler, IPPROTO_UDP)) {
		fprintf (stderr, "%s", "unable to register (C2_SESSION_PROGRAM, C2_SESSION_VER, udp).");
		exit(1);
	}

	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (transp == NULL) {
		fprintf (stderr, "%s", "cannot create tcp service.");
		exit(1);
	}
	if (!svc_register(transp, programm, ver, handler, IPPROTO_TCP)) {
		fprintf (stderr, "%s", "unable to register (%lu, %lu, tcp).",
			 programm, ver);
		exit(1);
	}

	svc_run ();
	fprintf (stderr, "%s", "svc_run returned");
	exit (1);
	/* NOTREACHED */
}

int c2_net_srv_stop(unsigned long int program_num, unsigned long ver);
{

}

