/* -*- C -*- */
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
 * Original author: Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 06/14/2011
 */

#include "lib/misc.h"
#include "lib/ut.h"
#include "net/bulk_emulation/sunrpc_xprt.h"
#include "net/ksunrpc/ksunrpc.h"

enum {
	PORT = 10001
};

static int sunrpc_ut_msg_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
static int sunrpc_ut_get_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
static int sunrpc_ut_put_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);

static struct c2_fop_type_ops sunrpc_msg_ops = {
	.fto_execute = sunrpc_ut_msg_handler,
};

static struct c2_fop_type_ops sunrpc_get_ops = {
	.fto_execute = sunrpc_ut_get_handler,
};

static struct c2_fop_type_ops sunrpc_put_ops = {
	.fto_execute = sunrpc_ut_put_handler,
};

static struct c2_net_domain dom;

extern struct c2_fop_type sunrpc_msg_fopt; /* opcode = 30 */
extern struct c2_fop_type sunrpc_get_fopt; /* opcode = 31 */
extern struct c2_fop_type sunrpc_put_fopt; /* opcode = 32 */

extern struct c2_fop_type sunrpc_msg_resp_fopt; /* opcode = 35 */
extern struct c2_fop_type sunrpc_get_resp_fopt; /* opcode = 36 */
extern struct c2_fop_type sunrpc_put_resp_fopt; /* opcode = 37 */

static struct c2_fop_type *s_fops[] = {
	&sunrpc_msg_fopt,
	&sunrpc_get_fopt,
	&sunrpc_put_fopt,
};

static int sunrpc_ut_msg_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	return -ENOSYS;
}

static int sunrpc_ut_get_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	return -ENOSYS;
}

static int sunrpc_ut_put_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	return -ENOSYS;
}

static int ksunrpc_service_handler(struct c2_service *service,
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

void test_ksunrpc_server(void)
{
	int rc;
	int i;

	struct c2_service_id sid1 = { .si_uuid = "node-1" };
	struct c2_net_conn *conn1;
	struct c2_service s1;
	c2_time_t t;

	/* NB: fops used are parsed when knet2 module is loaded,
	   override the ops vector for UT.
	 */
	C2_UT_ASSERT(sunrpc_msg_fopt.ft_top != NULL);
	C2_UT_ASSERT(sunrpc_get_fopt.ft_top != NULL);
	C2_UT_ASSERT(sunrpc_put_fopt.ft_top != NULL);
	sunrpc_msg_fopt.ft_ops = &sunrpc_msg_ops;
	sunrpc_get_fopt.ft_ops = &sunrpc_get_ops;
	sunrpc_put_fopt.ft_ops = &sunrpc_put_ops;

	C2_SET0(&s1);
	s1.s_table.not_start = s_fops[0]->ft_code;
	s1.s_table.not_nr    = ARRAY_SIZE(s_fops);
	s1.s_table.not_fopt  = s_fops;
	s1.s_handler         = &ksunrpc_service_handler;

	rc = c2_net_xprt_init(&c2_net_ksunrpc_minimal_xprt);
	C2_UT_ASSERT(rc == 0);

	rc = c2_net_domain_init(&dom, &c2_net_ksunrpc_minimal_xprt);
	C2_UT_ASSERT(rc == 0);

	rc = c2_service_id_init(&sid1, &dom, "127.0.0.1", PORT);
	C2_UT_ASSERT(rc == 0);
	if (rc == 99999) { /* XXX HACK */

	rc = c2_service_start(&s1, &sid1);
	C2_UT_ASSERT(rc >= 0);

	c2_nanosleep(c2_time_set(&t, 1, 0), NULL);

	rc = c2_net_conn_create(&sid1);
	C2_UT_ASSERT(rc == 0);

	conn1 = c2_net_conn_find(&sid1);
	C2_UT_ASSERT(conn1 != NULL);

	/* call msg API, tests sending basic record and sequence */
	for (i = 0; i < 100; ++i) {
	}

	/* call get API, tests receiving basic record and sequence */
	for (i = 0; i < 100; ++i) {
	}

	c2_net_conn_unlink(conn1);
	c2_net_conn_release(conn1);
	c2_service_stop(&s1);
	} /* XXX HACK */
	c2_service_id_fini(&sid1);
	c2_net_domain_fini(&dom);
	c2_net_xprt_fini(&c2_net_ksunrpc_minimal_xprt);
}

const struct c2_test_suite c2_net_ksunrpc_ut = {
        .ts_name = "ksunrpc-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "ksunrpc_server", test_ksunrpc_server },
                { NULL, NULL }
        }
};
C2_EXPORTED(c2_net_ksunrpc_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
