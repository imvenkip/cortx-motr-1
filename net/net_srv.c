int init_srv(unsigned long int program_num, unsigned long ver, rpc_hanlder hanlder)
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