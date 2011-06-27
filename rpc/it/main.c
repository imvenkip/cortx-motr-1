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


/**     
   Context for a ping client or server.
 */     
struct ping_ctx {
	/* Transport structure */
        struct c2_net_xprt			*pc_xprt;
	/* Network domain */
        struct c2_net_domain		 	 pc_dom; 
	/* Client hostname */
        const char				*pc_chostname;
	/* Client port */
        int				 	 pc_cport;
	/* Server hostname */
        const char				*pc_shostname;
	/* Server port */
        int					 pc_sport;
	/* Client end point */
        struct c2_net_end_point			*pc_cep;
	/* Server end point */
        struct c2_net_end_point			*pc_sep;
	/* RPC connection */
	struct c2_rpc_conn			*pc_conn;
};

enum {
	CLIENT_PORT = 32123,
	SERVER_PORT = 12321,
};

enum {
	LEN = 36,
};

enum {
	RID = 1,
};

struct ping_ctx		cctx;

/** Forward declaration. Actual code used from bulk_ping */
int canon_host(const char *hostname, char *buf, size_t bufsiz);

int main(int argc, char *argv[])
{
	int			 rc;
	bool			 client = false;
	const char		*client_name = NULL;
	int			 client_port = 0;
	bool			 server = false;
	const char		*server_name = NULL;
	int			 server_port = 0;
	char			 addr[LEN];
	char			 hostbuf[LEN];

	rc = C2_GETOPTS("rpcping", argc, argv,
		C2_FLAGARG('c', "run client", &client),
		C2_STRINGARG('C', "client hostname",
			LAMBDA(void, (const char *str) {client_name = str; })),
		C2_FORMATARG('p', "client port", "%i", &client_port),
		C2_FLAGARG('c', "run server", &server),
		C2_STRINGARG('C', "server hostname",
			LAMBDA(void, (const char *str) {server_name = str; })),
		C2_FORMATARG('p', "server port", "%i", &server_port));
	if (rc != 0)
		return rc;
	
	/* Set defaults */
	if(cctx.pc_chostname == NULL)
		cctx.pc_chostname = "localhost";
	if(cctx.pc_shostname == NULL)
		cctx.pc_shostname = "localhost";
	if(cctx.pc_cport == 0)
		cctx.pc_cport = CLIENT_PORT;
	if(cctx.pc_sport == 0)
		cctx.pc_sport = SERVER_PORT;
	/* Set if passed through command line interface */
	if(client_name)
		cctx.pc_chostname = client_name;
	if(client_port)
		cctx.pc_cport = client_port;
	if(server_name)
		cctx.pc_shostname = server_name;
	if(server_port)
		cctx.pc_sport = server_port;

	if(client) {

		/* Init Bulk sunrpc transport */
		cctx.pc_xprt = &c2_net_bulk_sunrpc_xprt;
		c2_net_xprt_init(cctx.pc_xprt);
		if(rc != 0) {
			printf("Failed to init transport\n");
		} else {
			printf("Bulk sunrpc transport init completed \n");
		}


		/* Resolve client hostname */
		rc = canon_host(cctx.pc_chostname, hostbuf, sizeof(hostbuf));
		if(rc != 0) {
			printf("Failed to canon host\n");
		} else {
			printf("Client Hostname Resolved \n");
		}

		/* Init client side network domain */
		rc = c2_net_domain_init(&cctx.pc_dom, cctx.pc_xprt);
		if(rc != 0) {
			printf("Failed to init domain\n");
		} else {
			printf("Domain init completed\n");
		}
		sprintf(addr, "%s:%u:%d", hostbuf, cctx.pc_cport, RID);
		printf("Client Addr = %s\n",addr);

		/* Create source endpoint for client side */
		rc = c2_net_end_point_create(&cctx.pc_cep,&cctx.pc_dom,addr); 
		if(rc != 0){
			printf("Failed to create endpoint\n");
		} else {
			printf("Server Endpoint created \n");
		}

		/* Resolve server hostname */
		rc = canon_host(cctx.pc_shostname, hostbuf, sizeof(hostbuf));
		if(rc != 0) {
			printf("Failed to canon host\n");
		} else {
			printf("Server Hostname Resolved \n");
		}
		sprintf(addr, "%s:%u:%d", hostbuf, cctx.pc_sport, RID);
		printf("Server Addr = %s\n",addr);

		/* Create destination endpoint for client i.e server endpoint */
		rc = c2_net_end_point_create(&cctx.pc_sep, &cctx.pc_dom, addr); 
		if(rc != 0){
			printf("Failed to create endpoint\n");
		} else {
			printf("Server Endpoint created \n");
		}

		/* Create RPC connection using new API 
		rc = c2_rpc_conn_create(&cctx.pc_conn, &cctx.pc_sep,
				&cctx.pc_cep); */	
		
	}

	return 0;
}
