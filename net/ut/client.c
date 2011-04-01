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
#include "net_fop.h"
#include "net_u.h"


static struct c2_addb_ctx net_ut_client_addb_ctx;

static const struct c2_addb_loc net_ut_client_addb_loc = {
	.al_name = "net-ut-client"
};

const struct c2_addb_ctx_type net_ut_client_addb_ctx_type = {
	.act_name = "net-ut-client-type",
};

enum {
	PORT = 10001
};

static struct c2_net_domain dom;
extern struct c2_fop_type c2_addb_record_fopt; /* opcode = 14 */

static struct c2_fop_type *net_ut_fopt[] = {
	&c2_nettest_fopt,
	&c2_addb_record_fopt
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

static void nettest_send(struct c2_net_conn *conn, int num)
{
	struct c2_fop        *f;
	struct c2_fop        *r;
	struct c2_nettest *fop;
	struct c2_nettest *rep;
	int result;
	static int addb_call_seq;

	addb_call_seq++;

	f = c2_fop_alloc(&c2_nettest_fopt, NULL);
	fop = c2_fop_data(f);
	r = c2_fop_alloc(&c2_nettest_fopt, NULL);
	rep = c2_fop_data(r);

	fop->siq_rc = num;
	result = netcall(conn, f, r);
	C2_UT_ASSERT(result == 0);

	C2_ADDB_ADD(&net_ut_client_addb_ctx, &net_ut_client_addb_loc,
	            c2_addb_func_fail, "from-addb-ut-client", addb_call_seq);

	rep = c2_fop_data(r);
/*	printf("GOT: %3i %3i: %6i %6i\n", num, result,
		fop->siq_rc, rep->siq_rc); */
	C2_UT_ASSERT(fop->siq_rc * fop->siq_rc == rep->siq_rc);

	c2_fop_free(r);
	c2_fop_free(f);

}

int nettest_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
        struct c2_nettest *in;
        struct c2_fop     *reply;
        struct c2_nettest *ex;

	in = c2_fop_data(fop);

        reply = c2_fop_alloc(&c2_nettest_fopt, NULL);
        C2_UT_ASSERT(reply != NULL);
        ex = c2_fop_data(reply);

	/* return the square of the requested number */
        ex->siq_rc =in->siq_rc * in->siq_rc;

        c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);
        return 1;
}

static int nettest_service_handler(struct c2_service *service,
				   struct c2_fop *fop,
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

	c2_addb_ctx_init(&net_ut_client_addb_ctx, &net_ut_client_addb_ctx_type,
			 &c2_addb_global_ctx);

        rc = nettest_fop_init();
        C2_UT_ASSERT(rc == 0);

	C2_SET0(&s1);
	s1.s_table.not_start = net_ut_fopt[0]->ft_code;
	s1.s_table.not_nr    = ARRAY_SIZE(net_ut_fopt);
	s1.s_table.not_fopt  = net_ut_fopt;
	s1.s_handler         = &nettest_service_handler;

	rc = c2_net_xprt_init(&c2_net_usunrpc_xprt);
	C2_UT_ASSERT(rc == 0);

	rc = c2_net_domain_init(&dom, &c2_net_usunrpc_xprt);
	C2_UT_ASSERT(rc == 0);

	rc = c2_service_id_init(&sid1, &dom, "127.0.0.1", PORT);
	C2_UT_ASSERT(rc == 0);

	rc = c2_service_start(&s1, &sid1);
	C2_UT_ASSERT(rc >= 0);

	sleep(1);

	rc = c2_net_conn_create(&sid1);
	C2_UT_ASSERT(rc == 0);

	conn1 = c2_net_conn_find(&sid1);
	C2_UT_ASSERT(conn1 != NULL);

	/* write addb record onto network */
	/* Use RPC */
	/* c2_addb_store_type     = C2_ADDB_REC_STORE_NETWORK; */
	/* c2_addb_net_add_p      = c2_addb_net_add; */
	/* c2_addb_store_net_conn = conn1; */
	//c2_addb_level_default  = AEL_ERROR;

	for (i = 0; i < 100; ++i) {
		sprintf(node_arg.si_uuid, "%d", i);
		nettest_send(conn1, i);
	}
	
	/* printf("rc = %d\n", rc); */
	/* printf("%s\n", node_ret.si_uuid); */

	c2_net_conn_unlink(conn1);
	c2_net_conn_release(conn1);
	c2_service_stop(&s1);
	c2_service_id_fini(&sid1);
	c2_net_domain_fini(&dom);
	c2_net_xprt_fini(&c2_net_usunrpc_xprt);
	nettest_fop_fini();
	c2_addb_ctx_fini(&net_ut_client_addb_ctx);
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
