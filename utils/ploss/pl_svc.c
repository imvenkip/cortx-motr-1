/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 */
/*
 * Packet loss testing tool.
 *
 * This tool is used to investigate the behavior of transport layer to simulate
 * the packet loss, reorder, duplication over the network.
 *
 * Written by Jay <jinshan.xiong@clusterstor.com>
 */

#include "pl.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sysexits.h>
#include <rpc/pmap_clnt.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "lib/misc.h"   /* C2_SET0 */

#ifndef SIG_PF
#define SIG_PF void(*)(int)
#endif

static void
plprog_1(struct svc_req *rqstp, register SVCXPRT *transp)
{
	union {
		struct c2_pl_ping ping_1_arg;
		struct c2_pl_config setconfig_1_arg;
		struct c2_pl_config getconfig_1_arg;
	} argument;
	union {
		struct c2_pl_ping_res ping_1_res;
		struct c2_pl_config_res setconfig_1_res;
		struct c2_pl_config_res getconfig_1_res;
	} result;
	bool_t retval;
	xdrproc_t _xdr_argument, _xdr_result;
	bool_t (*local)(char *, void *, struct svc_req *);

	switch (rqstp->rq_proc) {
	case NULLPROC:
		(void) svc_sendreply (transp, (xdrproc_t) xdr_void, (char *)NULL);
		return;

	case PING:
		_xdr_argument = (xdrproc_t) xdr_c2_pl_ping;
		_xdr_result = (xdrproc_t) xdr_c2_pl_ping_res;
		local = (bool_t (*) (char *, void *,  struct svc_req *))ping_1_svc;
		break;

	case SETCONFIG:
		_xdr_argument = (xdrproc_t) xdr_c2_pl_config;
		_xdr_result = (xdrproc_t) xdr_c2_pl_config_res;
		local = (bool_t (*) (char *, void *,  struct svc_req *))setconfig_1_svc;
		break;

	case GETCONFIG:
		_xdr_argument = (xdrproc_t) xdr_c2_pl_config;
		_xdr_result = (xdrproc_t) xdr_c2_pl_config_res;
		local = (bool_t (*) (char *, void *,  struct svc_req *))getconfig_1_svc;
		break;

	default:
		svcerr_noproc (transp);
		return;
	}
	C2_SET0(&argument);
	if (!svc_getargs (transp, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) {
		svcerr_decode (transp);
		return;
	}
	retval = (bool_t) (*local)((char *)&argument, (void *)&result, rqstp);
	if (retval > 0 && !svc_sendreply(transp, (xdrproc_t) _xdr_result, (char *)&result)) {
		svcerr_systemerr (transp);
	}
	if (!svc_freeargs (transp, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) {
		fprintf (stderr, "%s", "unable to free arguments");
		exit(EX_SOFTWARE);
	}
	if (!plprog_1_freeresult (transp, _xdr_result, (caddr_t) &result))
		fprintf (stderr, "%s", "unable to free results");

	return;
}

static void usage(const char *prog)
{
        fprintf(stderr, "Usage: %s [-p threads]\n"
                        "-p threads: How many threads will be started to handle the ping requests.\n"
                        "\tThe default thread # is 4.\n",
                prog);
        exit(EX_USAGE);
}

int
main (int argc, char **argv)
{
	register SVCXPRT *transp;
        char *endptr;
        int threads = 4;
        int c;

        while((c = getopt(argc, argv, "hp:")) != -1) {
                switch(c) {
                case 'p':
                        threads = strtol(argv[1], &endptr, 10);
                        if (*endptr) {
                                fprintf(stderr, "invalid parameter for thread #\n");
                                exit(EX_USAGE);
                        }
                        if (threads > 256)
                                threads = 256;
                case 'h':
                default:
                        usage(argv[0]);
                }
        }

        srandom(time(NULL));

	pmap_unset (PLPROG, PLVER);

	transp = svcudp_create(RPC_ANYSOCK);
	if (transp == NULL) {
		fprintf (stderr, "%s", "cannot create udp service.");
		exit(EX_OSERR);
	}
	if (!svc_register(transp, PLPROG, PLVER, plprog_1, IPPROTO_UDP)) {
		fprintf (stderr, "%s", "unable to register (PLPROG, PLVER, udp).");
		exit(EX_OSERR);
	}

	transp = svctcp_create(RPC_ANYSOCK, 0, 0);
	if (transp == NULL) {
		fprintf (stderr, "%s", "cannot create tcp service.");
		exit(EX_OSERR);
	}
	if (!svc_register(transp, PLPROG, PLVER, plprog_1, IPPROTO_TCP)) {
		fprintf (stderr, "%s", "unable to register (PLPROG, PLVER, tcp).");
		exit(EX_OSERR);
	}

	svc_run ();
	fprintf (stderr, "%s", "svc_run returned");
	exit (EX_SOFTWARE);
	/* NOTREACHED */
}
