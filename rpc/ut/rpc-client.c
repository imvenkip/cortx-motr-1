#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>     /* feof, fscanf, ... */
#include <err.h>

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/cdefs.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "addb/addb.h"
#include "net/net.h"
#include "net/usunrpc/usunrpc.h"

#include "stob/stob.h"
#include "stob/linux.h"
#include "colibri/init.h"
#include "net/net.h"
#include "net/usunrpc/usunrpc.h"
#include "fop/fop.h"

#include "rpc/session_u.h"
#include "rpc/session_fops.h"

//int got_quit = 0;

static int netcall(struct c2_net_conn *conn, struct c2_fop *arg,
                   struct c2_fop *ret)
{
        struct c2_net_call call = {
                .ac_arg = arg,
                .ac_ret = ret
        };
        return c2_net_cli_call(conn, &call);
}

static void rpc_conn_create_send(struct c2_net_conn *conn)
{
	 int result;
        struct c2_fop                    *f;
        struct c2_fop                    *r;
        struct c2_rpc_conn_create        *fop;
        struct c2_rpc_conn_create_rep  *rep;
 
        f = c2_fop_alloc(&c2_rpc_conn_create_fopt, NULL);
        fop = c2_fop_data(f);
        r = c2_fop_alloc(&c2_rpc_conn_create_rep_fopt, NULL);
        rep = c2_fop_data(r);
	C2_ASSERT(fop != NULL);
        fop->rcc_temp = 121;
	C2_ASSERT(rep !=NULL);

        result = netcall(conn, f, r);
        printf("GOT: %i %u %llu\n", result, rep->rccr_rc,
			(unsigned long long) rep->rccr_snd_id);
}

static void rpc_conn_terminate_send(struct c2_net_conn *conn)
{
	 int result;
        struct c2_fop                    *f;
        struct c2_fop                    *r;
        struct c2_rpc_conn_terminate        *fop;
        struct c2_rpc_conn_terminate_rep  *rep;
 
        f = c2_fop_alloc(&c2_rpc_conn_terminate_fopt, NULL);
        fop = c2_fop_data(f);
        r = c2_fop_alloc(&c2_rpc_conn_terminate_rep_fopt, NULL);
        fop->ct_snd_id = 100;

        result = netcall(conn, f, r);
        rep = c2_fop_data(r);
        printf("GOT: %i %d\n", result, rep->ctr_rc);
}

static void rpc_session_create_send(struct c2_net_conn *conn)
{
	 int result;
        struct c2_fop                    *f;
        struct c2_fop                    *r;
        struct c2_rpc_session_create        *fop;
        struct c2_rpc_session_create_rep  *rep;
 
        f = c2_fop_alloc(&c2_rpc_session_create_fopt, NULL);
        fop = c2_fop_data(f);
        r = c2_fop_alloc(&c2_rpc_session_create_rep_fopt, NULL);
        rep = c2_fop_data(r);
        fop->rsc_snd_id = 100;

        result = netcall(conn, f, r);
        printf("GOT: %i %u %llu\n", result, rep->rscr_status, 
		(unsigned long long) rep->rscr_session_id);
}
static void rpc_session_destroy_send(struct c2_net_conn *conn)
{
	int result;
	struct c2_fop                    *f;
	struct c2_fop                    *r;
	struct c2_rpc_session_destroy        *fop;
	struct c2_rpc_session_destroy_rep  *rep;
 
	f = c2_fop_alloc(&c2_rpc_session_destroy_fopt, NULL);
	fop = c2_fop_data(f);
	r = c2_fop_alloc(&c2_rpc_session_destroy_rep_fopt, NULL);
	rep = c2_fop_data(r);
	fop->rsd_session_id = 100;

	result = netcall(conn, f, r);
	printf("GOT: %i %u\n", result, rep->rsdr_status);
}
int main(int argc, char **argv)
{
        int result;

        struct c2_service_id    sid = { .si_uuid = "ABCDEFG" };
        struct c2_net_domain    ndom;
        struct c2_net_conn     *conn;

        setbuf(stdout, NULL);
        setbuf(stderr, NULL);

        if (argc != 3) {
                fprintf(stderr, "%s host port\n", argv[0]);
                return -1;
        }

        result = c2_init();
        C2_ASSERT(result == 0);

        result = c2_rpc_session_fop_init();
        C2_ASSERT(result == 0);
	
	result = c2_net_xprt_init(&c2_net_usunrpc_xprt);
        C2_ASSERT(result == 0);

        result = c2_net_domain_init(&ndom, &c2_net_usunrpc_xprt);
        C2_ASSERT(result == 0);

        result = c2_service_id_init(&sid, &ndom, argv[1], atoi(argv[2]));
        C2_ASSERT(result == 0);

        result = c2_net_conn_create(&sid);
        C2_ASSERT(result == 0);

        conn = c2_net_conn_find(&sid);
        C2_ASSERT(conn != NULL);

	  while (!feof(stdin)) {
                char cmd;
                int n;

                n = scanf("%c", &cmd);
                if (n != 1)
                        err(1, "wrong conversion: %i", n);
                switch (cmd) {
                case 'a':
                        rpc_conn_create_send(conn);
                        break;
                case 'b':
                        rpc_conn_terminate_send(conn);
                        break;
                case 'c':
                        rpc_session_create_send(conn);
                        break;
                case 'd':
                        rpc_session_destroy_send(conn);
                        break;
                default:
                        err(1, "Unknown command '%c'", cmd);
                }
  //              if (got_quit)
    //                    break;
		n = scanf(" \n");
	}

	 c2_net_conn_unlink(conn);
        c2_net_conn_release(conn);

        c2_service_id_fini(&sid);
        c2_net_domain_fini(&ndom);
        c2_net_xprt_fini(&c2_net_usunrpc_xprt);
        c2_rpc_session_fop_fini();
        c2_fini();

        return 0;
}

