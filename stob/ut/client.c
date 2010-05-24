#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>    /* memset */
#include <errno.h>

#include "lib/assert.h"
#include "lib/memory.h"
#include "net/net.h"
#include "net/sunrpc/sunrpc.h"

#include "stob/stob.h"
#include "stob/linux.h"

#include "io_fop.h"

/**
   @addtogroup stob
   @{
 */

static const struct c2_rpc_op create_op = {
	.ro_op          = SIF_CREAT,
	.ro_arg_size    = sizeof(struct c2_stob_io_create_fop),
	.ro_xdr_arg     = (c2_xdrproc_t)xdr_c2_stob_io_create_fop,
	.ro_result_size = sizeof(struct c2_stob_io_create_rep_fop),
	.ro_xdr_result  = (c2_xdrproc_t)xdr_c2_stob_io_create_rep_fop,
	.ro_handler     = NULL
};

/**
   Simple server for unit-test purposes. 
 */
int main(int argc, char **argv)
{
	int result;

	struct c2_service_id    sid = { .si_uuid = "UUURHG" };
	struct c2_rpc_op_table *ops;
	struct c2_net_domain    ndom;
	struct c2_net_conn     *conn;

	struct c2_stob_io_create_fop     fop;
	struct c2_stob_io_create_rep_fop rep;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	result = c2_net_init();
	C2_ASSERT(result == 0);

	result = c2_net_xprt_init(&c2_net_user_sunrpc_xprt);
	C2_ASSERT(result == 0);

	result = c2_net_domain_init(&ndom, &c2_net_user_sunrpc_xprt);
	C2_ASSERT(result == 0);

	result = c2_service_id_init(&sid, &ndom, "127.0.0.1", PORT);
	C2_ASSERT(result == 0);

	c2_rpc_op_table_init(&ops);
	C2_ASSERT(ops != NULL);

	result = c2_rpc_op_register(ops, &create_op);
	C2_ASSERT(result == 0);

	result = c2_net_conn_create(&sid);
	C2_ASSERT(result == 0);

	conn = c2_net_conn_find(&sid);
	C2_ASSERT(conn != NULL);

	fop.sic_object.f_d1 = 7;
	fop.sic_object.f_d2 = 8;
	
	result = c2_net_cli_call(conn, ops, SIF_CREAT, &fop, &rep);
	C2_ASSERT(result == 0);
	C2_ASSERT(rep.sicr_rc == 0);

	c2_net_conn_unlink(conn);
	c2_net_conn_release(conn);

	c2_rpc_op_table_fini(ops);

	c2_service_id_fini(&sid);
	c2_net_domain_fini(&ndom);
	c2_net_xprt_fini(&c2_net_user_sunrpc_xprt);
	c2_net_fini();

	return 0;
}

/** @} end group stob */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
