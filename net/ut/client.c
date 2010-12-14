#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "lib/ut.h"
#include "lib/assert.h"
#include "lib/misc.h"   /* C2_SET0 */
#include "lib/memory.h"

#include "net/net.h"
#include "net/xdr.h"
#include "net/usunrpc/usunrpc.h"

#include "fop/fop.h"
#include "io_fop.h"
#include "io_u.h"

enum {
	PORT = 10001
};

static struct c2_net_domain dom;

static struct c2_fop_type *fopt[] = {
	&c2_io_nettest_fopt
};

static int netcall(struct c2_net_conn *conn, struct c2_fop *arg,
		   struct c2_fop *ret)
{
	struct c2_net_call call = {
		.ac_arg = arg,
		.ac_ret = ret
	};
	return c2_net_cli_call(conn, &call);
}

static void nettest_send(struct c2_net_conn *conn)
{
	struct c2_fop        *f;
	struct c2_fop        *r;
	struct c2_io_nettest *fop;
	struct c2_io_nettest *rep;
	int result;

	f = c2_fop_alloc(&c2_io_nettest_fopt, NULL);
	fop = c2_fop_data(f);
	r = c2_fop_alloc(&c2_io_nettest_fopt, NULL);
	rep = c2_fop_data(r);

	result = netcall(conn, f, r);
	CU_ASSERT(result == 0);
	rep = c2_fop_data(r);
	/* printf("GOT: %i %i\n", result, rep->siq_rc); */
}

int nettest_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
        struct c2_fop        *reply;
        struct c2_io_nettest *ex;

        reply = c2_fop_alloc(&c2_io_nettest_fopt, NULL);
        CU_ASSERT(reply != NULL);
        ex = c2_fop_data(reply);

        ex->siq_rc = 42;

        c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);
        return 1;
}

static int io_handler(struct c2_service *service, struct c2_fop *fop,
                      void *cookie)
{
        struct c2_fop_ctx ctx;

        ctx.ft_service = service;
        ctx.fc_cookie  = cookie;
        /* printf("Got fop: code = %d, name = %s\n", */
	/*        fop->f_type->ft_code, fop->f_type->ft_name); */
        return fop->f_type->ft_ops->fto_execute(fop, &ctx);
}

void test_net_client(void)
{
	int rc;
	int i;

	struct c2_service_id sid1 = { .si_uuid = "node-1" };
	struct c2_net_conn *conn1;
	struct c2_service_id  node_arg = { .si_uuid = {0} };
	/* struct c2_service_id  node_ret = { .si_uuid = {0} }; */
	struct c2_service s1;

	C2_SET0(&s1);
	s1.s_table.not_start = fopt[0]->ft_code;
	s1.s_table.not_nr    = ARRAY_SIZE(fopt);
	s1.s_table.not_fopt  = fopt;
	s1.s_handler         = &io_handler;

        rc = io_fop_init();
        CU_ASSERT(rc == 0);

	rc = c2_net_xprt_init(&c2_net_usunrpc_xprt);
	CU_ASSERT(rc == 0);

	rc = c2_net_domain_init(&dom, &c2_net_usunrpc_xprt);
	CU_ASSERT(rc == 0);

	rc = c2_service_id_init(&sid1, &dom, "127.0.0.1", PORT);
	CU_ASSERT(rc == 0);

	rc = c2_service_start(&s1, &sid1);
	CU_ASSERT(rc >= 0);

	sleep(1);

	rc = c2_net_conn_create(&sid1);
	CU_ASSERT(rc == 0);

	conn1 = c2_net_conn_find(&sid1);
	CU_ASSERT(conn1 != NULL);

	for (i = 0; i < 100; ++i) {
		sprintf(node_arg.si_uuid, "%d", i);
		nettest_send(conn1);
	}
	
	/* printf("rc = %d\n", rc); */
	/* printf("%s\n", node_ret.si_uuid); */

	c2_net_conn_unlink(conn1);
	c2_net_conn_release(conn1);
	c2_service_stop(&s1);
	c2_service_id_fini(&sid1);
	c2_net_domain_fini(&dom);
	c2_net_xprt_fini(&c2_net_usunrpc_xprt);
	io_fop_fini();
}

const struct c2_test_suite net_client_ut = {
        .ts_name = "net_client-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "net_client", test_net_client },
                { NULL, NULL }
        }
};

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
