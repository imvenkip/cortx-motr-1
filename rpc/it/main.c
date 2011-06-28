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
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 06/27/2011
 */


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "colibri/init.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/getopts.h"
#include "lib/memory.h"
#include "lib/misc.h" /* C2_SET0 */
#include "lib/thread.h"
#include "net/net.h"
#include "net/net_internal.h"
#include "net/bulk_sunrpc.h"
#include "rpc/session.h"
#include "rpc/rpccore.h"


/**     
   Context for a ping client or server.
 */     
struct ping_ctx {
	/* Transport structure */
        struct c2_net_xprt			*pc_xprt;
	/* Network domain */
        struct c2_net_domain			 pc_dom; 
	/* Local hostname */
        const char				*pc_lhostname;
	/* Local port */
        int					 pc_lport;
	/* Remote hostname */
        const char				*pc_rhostname;
	/* Remote port */
        int					 pc_rport;
	/* Local source end point */
        struct c2_net_end_point			*pc_lep;
	/* Remote destination end point */
        struct c2_net_end_point			*pc_rep;
	/* RPC connection */
	struct c2_rpc_conn			 pc_conn;
	/* rpcmachine */
	struct c2_rpcmachine			 pc_rpc_mach;
	/* db for rpcmachine */
	struct c2_dbenv				 pc_db;
	/* db name */
	const char				*pc_db_name;
	/* cob domain */
	struct c2_cob_domain			 pc_cob_domain;
	/* cob domain id */
	struct c2_cob_domain_id			 pc_cob_dom_id;
	/* rpc session */
	struct c2_rpc_session			 pc_rpc_session;
	/* number of slots */
	int					 pc_nr_slots;
};

/* Default port values */
enum {
	CLIENT_PORT = 32123,
	SERVER_PORT = 12321,
};

/* Default address length */
enum {
	ADDR_LEN = 36,
};

/* Default RID */
enum {
	RID = 1,
};

/* Default number of slots */
enum {
	NR_SLOTS = 10,
};

/* Global client ping context */
struct ping_ctx		cctx;

/* Global server ping context */
struct ping_ctx		sctx;

/** Forward declaration. Actual code in bulk_ping */
int canon_host(const char *hostname, char *buf, size_t bufsiz);

/* Do cleanup */
void do_cleanup()
{
}

/* Poll the server */
void server_poll()
{
                char cli_buf[ADDR_LEN];

                printf("Type \"quit\" or ^D to terminate\n");
                while (fgets(cli_buf, ADDR_LEN, stdin)) {
                        if (strcmp(cli_buf, "quit\n") == 0)
                                break;
                }
}

/* Create the server*/
void server_init()
{
	int	rc = 0;
	char	addr[ADDR_LEN];
	char	hostbuf[ADDR_LEN];

	/* Init Bulk sunrpc transport */
	sctx.pc_xprt = &c2_net_bulk_sunrpc_xprt;
	c2_net_xprt_init(sctx.pc_xprt);
	if(rc != 0) {
		printf("Failed to init transport\n");
		goto cleanup;
	} else {
		printf("Bulk sunrpc transport init completed \n");
	}


	/* Resolve client hostname */
	rc = canon_host(sctx.pc_lhostname, hostbuf, sizeof(hostbuf));
	if(rc != 0) {
		printf("Failed to canon host\n");
		goto cleanup;
	} else {
		printf("Server Hostname Resolved \n");
	}

	/* Init server side network domain */
	rc = c2_net_domain_init(&sctx.pc_dom, sctx.pc_xprt);
	if(rc != 0) {
		printf("Failed to init domain\n");
		goto cleanup;
	} else {
		printf("Domain init completed\n");
	}
	sprintf(addr, "%s:%u:%d", hostbuf, sctx.pc_lport, RID);
	printf("Server Addr = %s\n",addr);

	/* Create source endpoint for server side */
	rc = c2_net_end_point_create(&sctx.pc_lep,&sctx.pc_dom,addr); 
	if(rc != 0){
		printf("Failed to create endpoint\n");
		goto cleanup;
	} else {
		printf("Server Endpoint created \n");
	}

	/* Resolve Client hostname */
	rc = canon_host(sctx.pc_rhostname, hostbuf, sizeof(hostbuf));
	if(rc != 0) {
		printf("Failed to canon host\n");
		goto cleanup;
	} else {
		printf("Client Hostname Resolved \n");
	}
	sprintf(addr, "%s:%u:%d", hostbuf, sctx.pc_rport, RID);
	printf("Client Addr = %s\n",addr);

	/* Create destination endpoint for server i.e client endpoint */
	rc = c2_net_end_point_create(&sctx.pc_rep, &sctx.pc_dom, addr); 
	if(rc != 0){
		printf("Failed to create endpoint\n");
		goto cleanup;
	} else {
		printf("Client Endpoint created \n");
	}

	/* Create RPC connection using new API 
	   rc = c2_rpc_conn_create(&cctx.pc_conn, &cctx.pc_sep,
	   &cctx.pc_cep); */	

	sctx.pc_db_name = "rpcping_db_server";
	sctx.pc_cob_dom_id.id =  13 ;

	/* Init the db */
	rc = c2_dbenv_init(&sctx.pc_db, sctx.pc_db_name, 0);
	if(rc != 0){
		printf("Failed to init dbenv\n");
		goto cleanup;
	} else {
		printf("DB init completed \n");
	}

	/* Init the cob domain */
	rc = c2_cob_domain_init(&sctx.pc_cob_domain, &sctx.pc_db,
			&sctx.pc_cob_dom_id);
	if(rc != 0){
		printf("Failed to init cob domain\n");
		goto cleanup;
	} else {
		printf("Cob Domain Init completed \n");
	}

	/* Init the rpcmachine */
	rc = c2_rpcmachine_init(&sctx.pc_rpc_mach, &sctx.pc_cob_domain, NULL); 
	if(rc != 0){
		printf("Failed to init rpcmachine\n");
		goto cleanup;
	} else {
		printf("RPC machine init completed \n");
	}

cleanup:
	do_cleanup();
}
/* Create the client */
void client_init()
{
	int	rc = 0;
	char	addr[ADDR_LEN];
	char	hostbuf[ADDR_LEN];

	/* Init Bulk sunrpc transport */
	cctx.pc_xprt = &c2_net_bulk_sunrpc_xprt;
	c2_net_xprt_init(cctx.pc_xprt);
	if(rc != 0) {
		printf("Failed to init transport\n");
		goto cleanup;
	} else {
		printf("Bulk sunrpc transport init completed \n");
	}


	/* Resolve client hostname */
	rc = canon_host(cctx.pc_lhostname, hostbuf, sizeof(hostbuf));
	if(rc != 0) {
		printf("Failed to canon host\n");
		goto cleanup;
	} else {
		printf("Client Hostname Resolved \n");
	}

	/* Init client side network domain */
	rc = c2_net_domain_init(&cctx.pc_dom, cctx.pc_xprt);
	if(rc != 0) {
		printf("Failed to init domain\n");
		goto cleanup;
	} else {
		printf("Domain init completed\n");
	}
	sprintf(addr, "%s:%u:%d", hostbuf, cctx.pc_lport, RID);
	printf("Client Addr = %s\n",addr);

	/* Create source endpoint for client side */
	rc = c2_net_end_point_create(&cctx.pc_lep,&cctx.pc_dom,addr); 
	if(rc != 0){
		printf("Failed to create endpoint\n");
		goto cleanup;
	} else {
		printf("Server Endpoint created \n");
	}

	/* Resolve server hostname */
	rc = canon_host(cctx.pc_rhostname, hostbuf, sizeof(hostbuf));
	if(rc != 0) {
		printf("Failed to canon host\n");
		goto cleanup;
	} else {
		printf("Server Hostname Resolved \n");
	}
	sprintf(addr, "%s:%u:%d", hostbuf, cctx.pc_rport, RID);
	printf("Server Addr = %s\n",addr);

	/* Create destination endpoint for client i.e server endpoint */
	rc = c2_net_end_point_create(&cctx.pc_rep, &cctx.pc_dom, addr); 
	if(rc != 0){
		printf("Failed to create endpoint\n");
		goto cleanup;
	} else {
		printf("Server Endpoint created \n");
	}

	/* Create RPC connection using new API 
	   rc = c2_rpc_conn_create(&cctx.pc_conn, &cctx.pc_sep,
	   &cctx.pc_cep); */	

	cctx.pc_db_name = "rpcping_db_client";
	cctx.pc_cob_dom_id.id =  12 ;

	/* Init the db */
	rc = c2_dbenv_init(&cctx.pc_db, cctx.pc_db_name, 0);
	if(rc != 0){
		printf("Failed to init dbenv\n");
		goto cleanup;
	} else {
		printf("DB init completed \n");
	}

	/* Init the cob domain */
	rc = c2_cob_domain_init(&cctx.pc_cob_domain, &cctx.pc_db,
			&cctx.pc_cob_dom_id);
	if(rc != 0){
		printf("Failed to init cob domain\n");
		goto cleanup;
	} else {
		printf("Cob Domain Init completed \n");
	}

	/* Init the rpcmachine */
	rc = c2_rpcmachine_init(&cctx.pc_rpc_mach, &cctx.pc_cob_domain, NULL); 
	if(rc != 0){
		printf("Failed to init rpcmachine\n");
		goto cleanup;
	} else {
		printf("RPC machine init completed \n");
	}

	/* Init the connection structure */
	rc = c2_rpc_conn_init(&cctx.pc_conn, &cctx.pc_rpc_mach); 
	if(rc != 0){
		printf("Failed to init rpc connection\n");
		goto cleanup;
	} else {
		printf("RPC connection init completed \n");
	}

	/* Create RPC connection */
	rc = c2_rpc_conn_create(&cctx.pc_conn, cctx.pc_rep); 
	if(rc != 0){
		printf("Failed to create rpc connection\n");
		goto cleanup;
	} else {
		printf("RPC connection created \n");
	}

	/* Init session */
	rc = c2_rpc_session_init(&cctx.pc_rpc_session, &cctx.pc_conn,
			cctx.pc_nr_slots);
	if(rc != 0){
		printf("Failed to init rpc session\n");
		goto cleanup;
	} else {
		printf("RPC session init completed\n");
	}

	/* Create RPC session */
	rc = c2_rpc_session_create(&cctx.pc_rpc_session);
	if(rc != 0){
		printf("Failed to create session\n");
		goto cleanup;
	} else {
		printf("RPC session created\n");
	}

cleanup:
	do_cleanup();
}

/* Main function for rpc ping */
int main(int argc, char *argv[])
{
	int			 rc;
	bool			 client = false;
	const char		*client_name = NULL;
	int			 client_port = 0;
	bool			 server = false;
	const char		*server_name = NULL;
	int			 server_port = 0;
	int			 nr_slots;

	rc = c2_init();
	if (rc != 0)
		return rc;

	rc = C2_GETOPTS("rpcping", argc, argv,
		C2_FLAGARG('c', "run client", &client),
		C2_STRINGARG('C', "client hostname",
			LAMBDA(void, (const char *str) {client_name = str; })),
		C2_FORMATARG('p', "client port", "%i", &client_port),
		C2_FLAGARG('s', "run server", &server),
		C2_STRINGARG('S', "server hostname",
			LAMBDA(void, (const char *str) {server_name = str; })),
		C2_FORMATARG('p', "server port", "%i", &server_port),
		C2_FORMATARG('l', "number of slots", "%i", &nr_slots));
	if (rc != 0)
		return rc;
	
	/* Set defaults */
	sctx.pc_lhostname = cctx.pc_lhostname = "localhost";
	sctx.pc_rhostname = cctx.pc_rhostname = "localhost";
	sctx.pc_rport = cctx.pc_lport = CLIENT_PORT;
	sctx.pc_lport = cctx.pc_rport = SERVER_PORT;
	cctx.pc_nr_slots = NR_SLOTS;

	/* Set if passed through command line interface */
	if(client_name)
		sctx.pc_rhostname = cctx.pc_lhostname = client_name;
	if(client_port)
		sctx.pc_rport = cctx.pc_lport = client_port;
	if(server_name)
		sctx.pc_lhostname = cctx.pc_rhostname = server_name;
	if(server_port)
		sctx.pc_lport = cctx.pc_rport = server_port;
	if(nr_slots)
		sctx.pc_nr_slots = cctx.pc_nr_slots = nr_slots;

	/* Client part */
	if(client) {
		client_init();
	}

	/* Server part */
	if(server) {
		server_init();
		server_poll();
	}

	return 0;
}
