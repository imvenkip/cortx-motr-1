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
#include "rpc/formation.h"
#include "ioservice/io_fops.h"
#include "ping_fop.h"
#include "ping_fom.h"
#ifdef __KERNEL__
#include "ioservice/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#include "ping_fop_u.h"
#endif
#include "stob/ut/io_fop.h"

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
	/* number of ping items */
	int					 pc_nr_ping_items;
	/* number of ping bytes */
	int					 pc_nr_ping_bytes;
	/* number of client threads */
	int					 pc_nr_client_threads;
	/* switch to identify which type of fops have to be sent to rpc */
	int					 pc_fop_switch;
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
	NR_SLOTS = 1,
};

/* Default number of ping items */
enum {
	NR_PING_ITEMS = 1,
};

/* Default number of ping bytes */
enum {
	NR_PING_BYTES = 8,
};

/* Default number of client threads*/
enum {
	NR_CLIENT_THREADS = 1,
};

/* Switch for PING or IO fops */
enum {
	PING = 1,
	IO,
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

	printf("\n########################################\n");
	printf("\n\nType \"quit\" or ^D to terminate\n\n");
	printf("\n########################################\n");
	while (fgets(cli_buf, ADDR_LEN, stdin)) {
		if (strcmp(cli_buf, "quit\n") == 0)
			break;
		else {
			printf("\n########################################\n");
			printf("\n\nType \"quit\" or ^D to terminate\n\n");
			printf("\n########################################\n");
		}
	}
}

/* Create dummy request handler */
void server_rqh_init(int dummy)
{
	struct c2_queue_link 	*q1;
	struct c2_rpc_item	*item;
	struct c2_fop		*fop;
	struct c2_fom		*fom = NULL;
	struct c2_clink		 clink;

	c2_queue_init(&exec_queue);
	c2_chan_init(&exec_chan);
	c2_clink_init(&clink, NULL);
	c2_clink_add(&exec_chan, &clink);
	C2_ASSERT(c2_queue_is_empty(&exec_queue));
        C2_ASSERT(c2_queue_get(&exec_queue) == NULL);
        C2_ASSERT(c2_queue_length(&exec_queue) == 0);

	while (1) {
		c2_chan_wait(&clink);
		if (!c2_queue_is_empty(&exec_queue)) {
			q1 = c2_queue_get(&exec_queue);
			C2_ASSERT(q1 != NULL);
			item = container_of(q1, struct c2_rpc_item,
				ri_dummy_qlinkage);
			fop = c2_rpc_item_to_fop(item);
			fop->f_type->ft_ops->fto_fom_init(fop, &fom);
			C2_ASSERT(fom != NULL);
			fom->fo_ops->fo_state(fom);
			}
		}
}

/* Fini the client*/
void client_fini()
{
	/* Fini the rpcmachine */
	c2_rpcmachine_fini(&cctx.pc_rpc_mach);

	/* Fini the local net endpoint. */
	c2_net_end_point_put(cctx.pc_lep);

	/* Fini the remote net endpoint. */
	c2_net_end_point_put(cctx.pc_rep);

	/* Fini the net domain */
	c2_net_domain_fini(&cctx.pc_dom);

	/* Fini the transport */
	c2_net_xprt_fini(cctx.pc_xprt);

        /* Fini the cob domain */
        c2_cob_domain_fini(&cctx.pc_cob_domain);

        /* Fini the db */
        c2_dbenv_fini(&cctx.pc_db);
}

/* Fini the server */
void server_fini()
{
	/* Fini the rpcmachine */
	c2_rpcmachine_fini(&sctx.pc_rpc_mach);

	/* Fini the net endpoint. */
	c2_net_end_point_put(sctx.pc_lep);

	/* Fini the net endpoint. */
	c2_net_end_point_put(sctx.pc_rep);

	/* Fini the net domain */
	c2_net_domain_fini(&sctx.pc_dom);

	/* Fini the transport */
	c2_net_xprt_fini(sctx.pc_xprt);

        /* Fini the cob domain */
        c2_cob_domain_fini(&sctx.pc_cob_domain);

        /* Fini the db */
        c2_dbenv_fini(&sctx.pc_db);
}

/* Create the server*/
void server_init(int dummy)
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
	   rc = c2_rpc_conn_establish(&cctx.pc_conn, &cctx.pc_sep,
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
	rc = c2_rpcmachine_init(&sctx.pc_rpc_mach, &sctx.pc_cob_domain,
			sctx.pc_lep);
	if(rc != 0){
		printf("Failed to init rpcmachine\n");
		goto cleanup;
	} else {
		printf("RPC machine init completed \n");
	}

cleanup:
	do_cleanup();
}

#define nfiles                   64
#define ndatafids                8
#define nfops                    256
extern struct c2_fop_file_fid          *form_fids;
extern uint64_t                        *file_offsets;
extern struct c2_fop_io_vec           **form_write_iovecs;

int c2_rpc_frm_item_populate_param(struct c2_rpc_item *item);
void init_fids();
void populate_fids();
void init_file_io_patterns();
struct c2_fop *form_get_new_fop();

/**
  Init the data required for IO fops
 */
void io_fop_data_init()
{
        C2_ALLOC_ARR(form_fids, nfiles);
        init_fids();
        populate_fids();
        C2_ALLOC_ARR(file_offsets, ndatafids);
        init_file_io_patterns();
        C2_ALLOC_ARR(form_write_iovecs, nfops);
}

/**
  Create a random IO fop (either read or write) and post it to rpc layer
 */
void send_random_io_fop(int nr)
{
/*
        struct c2_fop           *fop;
        struct c2_rpc_item      *item = NULL;

        fop = form_get_new_fop();
        item = &fop->f_item;
        c2_rpc_frm_item_populate_param(&fop->f_item);
        item->ri_deadline = 0;
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
	item->ri_group = NULL;
	item->ri_mach = &cctx.pc_rpc_mach;
        c2_rpc_item_attach(item);
        item->ri_session = &cctx.pc_rpc_session;
        c2_rpc_post(item);
*/
}

/**
  Create a ping fop and post it to rpc layer
 */
void send_ping_fop(int nr)
{
	struct c2_fop                   *fop = NULL;
	struct c2_fop_ping		*ping_fop = NULL;
	struct c2_rpc_item		*item = NULL;
	uint32_t			 nr_mod;
	uint32_t			 nr_arr_member;
	int				 i;

	nr_mod = cctx.pc_nr_ping_bytes % 8;
	if (nr_mod == 0)
		nr_arr_member = cctx.pc_nr_ping_bytes / 8;
	else
		nr_arr_member = (cctx.pc_nr_ping_bytes / 8) + 1;
	fop = c2_fop_alloc(&c2_fop_ping_fopt, NULL);
	C2_ASSERT(fop != NULL);
	ping_fop = c2_fop_data(fop);
	ping_fop->fp_arr.f_count = nr_arr_member;
	C2_ALLOC_ARR(ping_fop->fp_arr.f_data, nr_arr_member);
	for (i = 0; i < nr_arr_member; i++) {
		ping_fop->fp_arr.f_data[i] = i+100;
	}
	item = &fop->f_item;
	c2_rpc_item_init(item);
	c2_rpc_frm_item_populate_param(&fop->f_item);
	item->ri_deadline = 0;
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
	item->ri_group = NULL;
	item->ri_mach = &cctx.pc_rpc_mach;
	c2_rpc_item_attach(item);
	item->ri_session = &cctx.pc_rpc_session;
	//c2_rpc_post(item);
}

/* Get stats from rpcmachine and print them */
void print_stats(bool client, bool server)
{
	struct c2_rpcmachine	*rpc_mach;
	struct c2_rpc_stats	*stats;
	uint64_t		 nsec;
	double			 sec = 0;
	double			 msec = 0;
	double			 thruput;

	if (client)
		rpc_mach = &cctx.pc_rpc_mach;
	else if (server)
		rpc_mach = &sctx.pc_rpc_mach;
	stats = &rpc_mach->cr_rpc_stats[C2_RPC_PATH_OUTGOING];
	printf("\n\n*********************************************\n");
	printf("Stats on Outgoing Path\n");
	printf("*********************************************\n");
	printf("Number of outgoing items = %lu\n",
			stats->rs_items_nr);
	printf("Number of outgoing bytes = %lu\n",
			stats->rs_bytes_nr);

	sec = 0;
	sec = c2_time_seconds(stats->rs_min_lat);
	nsec = c2_time_nanoseconds(stats->rs_min_lat);
	sec += (double) nsec/1000000000;
	msec = (double) sec * 1000;
	printf("\nMin latency    (msecs)   = %lf\n", msec);

	thruput = (double)stats->rs_bytes_nr/(sec*1000000);
	printf("Max Throughput (MB/sec)  = %lf\n", thruput);

	sec = 0;
	sec = c2_time_seconds(stats->rs_max_lat);
	nsec = c2_time_nanoseconds(stats->rs_max_lat);
	sec += (double) nsec/1000000000;
	msec = (double) sec * 1000;
	printf("\nMax latency    (msecs)   = %lf\n", msec);

	thruput = (double)stats->rs_bytes_nr/(sec*1000000);
	printf("Min Throughput (MB/sec)  = %lf\n", thruput);

	sec = 0;
	sec = c2_time_seconds(stats->rs_avg_lat);
	nsec = c2_time_nanoseconds(stats->rs_avg_lat);
	sec += (double) nsec/1000000000;
	msec = (double) sec * 1000;
	printf("\nAvg latency    (msecs)   = %lf\n", msec);

	thruput = (double)stats->rs_bytes_nr/(sec*1000000);
	printf("Avg Throughput (MB/sec)  = %lf\n", thruput);
	printf("*********************************************\n");

	stats = &rpc_mach->cr_rpc_stats[C2_RPC_PATH_INCOMING];

	printf("\n\n*********************************************\n");
	printf("Stats on Incoming Path\n");
	printf("*********************************************\n");
	printf("Number of incoming items = %lu\n",
			stats->rs_items_nr);
	printf("Number of incoming bytes = %lu\n",
			stats->rs_bytes_nr);

	sec = 0;
	sec = c2_time_seconds(stats->rs_min_lat);
	nsec = c2_time_nanoseconds(stats->rs_min_lat);
	sec += (double) nsec/1000000000;
	msec = (double) sec * 1000;
	printf("\nMin latency    (msecs)   = %lf\n", msec);

	thruput = (double)stats->rs_bytes_nr/(sec*1000000);
	printf("Max Throughput (MB/sec)  = %lf\n", thruput);

	sec = 0;
	sec = c2_time_seconds(stats->rs_max_lat);
	nsec = c2_time_nanoseconds(stats->rs_max_lat);
	sec += (double) nsec/1000000000;
	msec = (double) sec * 1000;
	printf("\nMax latency    (msecs)   = %lf\n", msec);

	thruput = (double)stats->rs_bytes_nr/(sec*1000000);
	printf("Min Throughput (MB/sec)  = %lf\n", thruput);

	sec = 0;
	sec = c2_time_seconds(stats->rs_avg_lat);
	nsec = c2_time_nanoseconds(stats->rs_avg_lat);
	sec += (double) nsec/1000000000;
	msec = (double) sec * 1000;
	printf("\nAvg latency    (msecs)   = %lf\n", msec);

	thruput = (double)stats->rs_bytes_nr/(sec*1000000);
	printf("Avg Throughput (MB/sec)  = %lf\n", thruput);
	printf("*********************************************\n");
}

/* Create the client */
void client_init()
{
	int			 rc = 0;
	int			 i;
	bool			 rcb;
	char			 addr[ADDR_LEN];
	char			 hostbuf[ADDR_LEN];
        c2_time_t		 timeout;
	struct c2_thread	*client_thread;

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
	   rc = c2_rpc_conn_establish(&cctx.pc_conn, &cctx.pc_sep,
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
	rc = c2_rpcmachine_init(&cctx.pc_rpc_mach, &cctx.pc_cob_domain,
			cctx.pc_lep);
	if(rc != 0){
		printf("Failed to init rpcmachine\n");
		goto cleanup;
	} else {
		printf("RPC machine init completed \n");
	}

	/* Init the connection structure */
	rc = c2_rpc_conn_init(&cctx.pc_conn, cctx.pc_rep, &cctx.pc_rpc_mach);
	if(rc != 0){
		printf("Failed to init rpc connection\n");
		goto cleanup;
	} else {
		printf("RPC connection init completed \n");
	}

	/* Create RPC connection */
	rc = c2_rpc_conn_establish(&cctx.pc_conn);
	if(rc != 0){
		printf("Failed to create rpc connection\n");
		goto cleanup;
	} else {
		printf("RPC connection created \n");
	}


        c2_time_now(&timeout);
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));

	rcb = c2_rpc_conn_timedwait(&cctx.pc_conn, C2_RPC_CONN_ACTIVE |
				   C2_RPC_CONN_FAILED, timeout);
	if (rcb) {
		if (cctx.pc_conn.c_state == C2_RPC_CONN_ACTIVE)
			printf("pingcli: Connection established\n");
		else if (cctx.pc_conn.c_state == C2_RPC_CONN_FAILED)
			printf("pingcli: conn create failed\n");
	} else
		printf("Timeout for conn create \n");

	/* Init session */
	rc = c2_rpc_session_init(&cctx.pc_rpc_session, &cctx.pc_conn,
			cctx.pc_nr_slots);
	printf("NR_SLOTS = %u\n",cctx.pc_nr_slots);
	printf("NR_SLOTS in session = %u\n",cctx.pc_rpc_session.s_nr_slots);
	if(rc != 0){
		printf("Failed to init rpc session\n");
		goto cleanup;
	} else {
		printf("RPC session init completed\n");
	}

	/* Create RPC session */
	rc = c2_rpc_session_establish(&cctx.pc_rpc_session);
	if(rc != 0){
		printf("Failed to create session\n");
		goto cleanup;
	} else {
		printf("RPC session created\n");
	}

        c2_time_now(&timeout);
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));
	/* Wait for session to become active */
	rcb = c2_rpc_session_timedwait(&cctx.pc_rpc_session,
			C2_RPC_SESSION_IDLE, timeout);
	if (rcb) {
		if (cctx.pc_rpc_session.s_state == C2_RPC_SESSION_IDLE)
			printf("pingcli: Session established\n");
		if (cctx.pc_rpc_session.s_state == C2_RPC_SESSION_FAILED)
			printf("pingcli: session create failed\n");
	} else
		printf("Timeout for session create \n");

	C2_ALLOC_ARR(client_thread, cctx.pc_nr_client_threads);
	io_fop_data_init();
	for (i = 0; i < cctx.pc_nr_client_threads; i++) {
		C2_SET0(&client_thread[i]);
		if (cctx.pc_fop_switch == PING)
			rc = C2_THREAD_INIT(&client_thread[i], int,
					NULL, &send_ping_fop,
					0, "client_%d", i);
		else if (cctx.pc_fop_switch == IO)
			rc = C2_THREAD_INIT(&client_thread[i], int,
					NULL, &send_random_io_fop,
					0, "client_%d", i);
		C2_ASSERT(rc == 0);
	}
	for (i = 0; i < cctx.pc_nr_client_threads; i++) {
		c2_thread_join(&client_thread[i]);
	}


	for (i = 0; i < cctx.pc_nr_ping_items; i++) {
		send_ping_fop(i);
	}

        c2_time_now(&timeout);
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));
	/* Wait for session to terminate */
	rcb = c2_rpc_session_timedwait(&cctx.pc_rpc_session,
			C2_RPC_SESSION_IDLE,
			timeout);
	C2_ASSERT(cctx.pc_rpc_session.s_state == C2_RPC_SESSION_IDLE);
	rc = c2_rpc_session_terminate(&cctx.pc_rpc_session);
	if(rc != 0){
		printf("Failed to terminate session\n");
		goto cleanup;
	} else {
		printf("RPC session terminate call successful\n");
	}

        c2_time_now(&timeout);
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));
	/* Wait for session to terminate */
	rcb = c2_rpc_session_timedwait(&cctx.pc_rpc_session,
			C2_RPC_SESSION_TERMINATED | C2_RPC_SESSION_FAILED,
			timeout);
	if (rcb) {
		if (cctx.pc_rpc_session.s_state == C2_RPC_SESSION_TERMINATED)
			printf("pingcli: Session terminated\n");
		if (cctx.pc_rpc_session.s_state == C2_RPC_SESSION_FAILED)
			printf("pingcli: session terminate failed\n");
	} else
		printf("Timeout for session terminate \n");

	/* Terminate RPC connection */
	rc = c2_rpc_conn_terminate(&cctx.pc_conn);
	if(rc != 0){
		printf("Failed to terminate rpc connection\n");
		goto cleanup;
	} else {
		printf("RPC connection terminate call successful \n");
	}


        c2_time_now(&timeout);
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));

	rcb = c2_rpc_conn_timedwait(&cctx.pc_conn, C2_RPC_CONN_TERMINATED |
				   C2_RPC_CONN_FAILED, timeout);
	if (rcb) {
		if (cctx.pc_conn.c_state == C2_RPC_CONN_TERMINATED)
			printf("pingcli: Connection terminated\n");
		else if (cctx.pc_conn.c_state == C2_RPC_CONN_FAILED)
			printf("pingcli: conn create failed\n");
		else
			printf("pingcli: conn INVALID!!!|n");
	} else
		printf("Timeout for conn terminate\n");
	c2_rpc_session_fini(&cctx.pc_rpc_session);
	c2_rpc_conn_fini(&cctx.pc_conn);

cleanup:
	do_cleanup();
}

/* Main function for rpc ping */
int main(int argc, char *argv[])
{
	bool			 pingfops = false;
	bool			 iofops = false;
	bool			 verbose = false;
	bool			 server = false;
	bool			 client = false;
	const char		*server_name = NULL;
	const char		*client_name = NULL;
	int			 server_port = 0;
	int			 client_port = 0;
	int			 nr_slots = 0;
	int			 nr_ping_bytes = 0;
	int			 nr_ping_item = 0;
	int			 nr_client_threads = 0;
	int			 rc;
	struct c2_thread	 server_thread;
	struct c2_thread	 server_rqh_thread;
	uint64_t		 c2_rpc_max_message_size;
	uint64_t		 c2_rpc_max_fragments_size;
	uint64_t		 c2_rpc_max_rpcs_in_flight;


	rc = c2_init();
	if (rc != 0)
		return rc;

	rc = io_fop_init();
	if (rc != 0)
		return rc;
	c2_ping_fop_init();

	rc = C2_GETOPTS("rpcping", argc, argv,
		C2_FLAGARG('c', "run client", &client),
		C2_FLAGARG('s', "run server", &server),
		C2_STRINGARG('C', "client hostname",
			LAMBDA(void, (const char *str) {client_name = str; })),
		C2_FORMATARG('p', "client port", "%i", &client_port),
		C2_STRINGARG('S', "server hostname",
			LAMBDA(void, (const char *str) {server_name = str; })),
		C2_FORMATARG('P', "server port", "%i", &server_port),
		C2_FORMATARG('b', "size in bytes", "%i", &nr_ping_bytes),
		C2_FORMATARG('t', "number of client threads", "%i", &nr_client_threads),
		C2_FORMATARG('l', "number of slots", "%i", &nr_slots),
		C2_FORMATARG('n', "number of ping items", "%i", &nr_ping_item),
		C2_FLAGARG('i', "send io fops", &iofops),
		C2_FLAGARG('d', "send ping data fops", &pingfops),
		C2_FLAGARG('v', "verbose", &verbose));
	if (rc != 0)
		return rc;

	/* Set defaults */
	sctx.pc_lhostname = cctx.pc_lhostname = "localhost";
	sctx.pc_rhostname = cctx.pc_rhostname = "localhost";
	sctx.pc_rport = cctx.pc_lport = CLIENT_PORT;
	sctx.pc_lport = cctx.pc_rport = SERVER_PORT;
	cctx.pc_nr_slots = NR_SLOTS;
	cctx.pc_nr_ping_items = NR_PING_ITEMS;
	cctx.pc_nr_ping_bytes = NR_PING_BYTES;
	cctx.pc_nr_client_threads = NR_CLIENT_THREADS;
	cctx.pc_fop_switch = PING;

	c2_rpc_max_message_size = 10*1024;
        /* Start with a default value of 8. The max value in Lustre, is
           limited to 32. */
        c2_rpc_max_rpcs_in_flight = 8;
        c2_rpc_max_fragments_size = 16;

        c2_rpc_frm_set_thresholds(c2_rpc_max_message_size,
                        c2_rpc_max_rpcs_in_flight, c2_rpc_max_fragments_size);

	/* Set if passed through command line interface */
	if (client_name)
		sctx.pc_rhostname = cctx.pc_lhostname = client_name;
	if (client_port != 0)
		sctx.pc_rport = cctx.pc_lport = client_port;
	if (server_name)
		sctx.pc_lhostname = cctx.pc_rhostname = server_name;
	if (server_port != 0)
		sctx.pc_lport = cctx.pc_rport = server_port;
	if (nr_slots != 0)
		sctx.pc_nr_slots = cctx.pc_nr_slots = nr_slots;
	if (nr_ping_item != 0)
		cctx.pc_nr_ping_items = nr_ping_item;
	if (nr_ping_bytes != 0)
		cctx.pc_nr_ping_bytes = nr_ping_bytes;
	if (nr_client_threads != 0)
		cctx.pc_nr_client_threads = nr_client_threads;
	if (pingfops)
		cctx.pc_fop_switch = PING;
	if (iofops)
		cctx.pc_fop_switch = IO;

	/* Client part */
	if(client) {
		client_init();
		if (verbose)
			print_stats(client, server);
		client_fini();
	}

	/* Server part */
	if(server) {
		/* server thread */
		C2_SET0(&server_thread);
		rc = C2_THREAD_INIT(&server_thread, int, NULL, &server_init,
				0, "ping_server");
		C2_SET0(&server_rqh_thread);
		rc = C2_THREAD_INIT(&server_rqh_thread, int, NULL,
				&server_rqh_init, 0, "ping_server_rqh");
		server_poll();
		if (verbose)
			print_stats(client, server);
		c2_thread_join(&server_thread);
		server_fini();

	}

	c2_ping_fop_fini();
	io_fop_fini();

	c2_fini();

	return 0;
}
