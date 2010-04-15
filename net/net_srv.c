#include "lib/cdefs.h" /* bool type */

#include <rpc/svc.h>

#include "net/net.h"
#include "net/net_internal.h"


void c2_net_srv_fn_generic(struct svc_req *req, SVCXPRT *transp,
			   struct c2_rpc_op_table *ops, void *arg, void *ret)
{
	int idx;
	bool_t retval;
	struct c2_rpc_op *op;

	if ((op = find_op(ops, rqstp->rq_proc)) == NULL) {
		svcerr_noproc (transp);
		return;
	}
	

	if (!svc_getargs (transp, ops->ro_xdr_argument, (caddr_t) arg)) {
		svcerr_decode (transp);
		return;
	}

	/** XXX need auth code */
	retval = (*local)(arg, ret);
	if (retval && !svc_sendreply(transp, ops->ro_xdr_result, ret)) {
		svcerr_systemerr (transp);
	}

	if (!svc_freeargs (transp, ops->ro_xdr_argument, (caddr_t) arg)) {
		/* bug */
	}

	/* XXX check result */
	c2_session_program_1_freeresult (transp, ops->ro_xdr_result, (caddr_t) ret))

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

