/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rajanikant Chirmade <Rajanikant_Chirmade@xyratex.com>
 * Original creation date: 01/06/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/ut.h"    /* C2_UT_ASSERT */
#include "lib/errno.h"
#include "lib/tlist.h"

#include "ut/rpc.h"
#include "rpc/rpclib.h"
#include "net/bulk_sunrpc.h"
#include "net/bulk_mem.h"
#include "reqh/reqh_service.h"
#include "colibri/colibri_setup.h"
#include "ioservice/io_service.h"

#include "colibri/colibri_setup.c"
#include "ioservice/io_service.c"

extern const struct c2_tl_descr bufferpools_tl;

 /* Colibri setup arguments. */
static char *ios_ut_bp_singledom_cmd[] = { "colibri_setup", "-r", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:2",
                                "-s", "ioservice"};
static char *ios_ut_bp_multidom_cmd[] = { "colibri_setup", "-r", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:2",
                                "-e", "bulk-mem:127.0.0.1:35678",
                                "-s", "ioservice"};
static char *ios_ut_bp_repeatdom_cmd[] = { "colibri_setup", "-r", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:2",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:1",
                                "-s", "ioservice"};
static char *ios_ut_bp_onerepeatdom_cmd[] = { "colibri_setup", "-r", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:2",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:1",
                                "-e", "bulk-mem:127.0.0.1:35678",
                                "-s", "ioservice"};
/*
  Transports used in colibri a context.
 */
static struct c2_net_xprt *cs_xprts[] = {
	&c2_net_bulk_sunrpc_xprt,
	&c2_net_bulk_mem_xprt
};

#define SERVER_LOG_FILE_NAME	"cs_ut.errlog"

C2_TL_DESCR_DEFINE(ut_rhctx, "reqh contexts", static, struct cs_reqh_context,
		   rc_linkage, rc_magic, CS_REQH_CTX_MAGIX,
		   CS_REQH_CTX_HEAD_MAGIX);

C2_TL_DEFINE(ut_rhctx, static, struct cs_reqh_context);

C2_TL_DESCR_DEFINE(ut_rhsrv, "reqh service", static, struct c2_reqh_service,
                   rs_linkage, rs_magic, C2_RHS_MAGIX, C2_RHS_MAGIX_HEAD);

C2_TL_DEFINE(ut_rhsrv, static, struct c2_reqh_service);

static int get_ioservice_buffer_pool_count(struct c2_rpc_server_ctx *sctx)
{
	struct cs_reqh_context		*reqh_ctx;
	int				 nbp;
	struct c2_reqh_io_service       *serv_obj = NULL;

	c2_tl_for(ut_rhctx, &sctx->rsx_colibri_ctx.cc_reqh_ctxs, reqh_ctx) {
		struct c2_reqh_service *reqh_ios;
		c2_tl_for(ut_rhsrv, &reqh_ctx->rc_reqh.rh_services,
			 reqh_ios) {
			if (strcmp(reqh_ios->rs_type->rst_name,
			           "ioservice") == 0) {
				serv_obj = container_of(reqh_ios,
						      struct c2_reqh_io_service,
						      rios_gen);
				break;
			}
		} c2_tl_endfor;
	} c2_tl_endfor;

	C2_UT_ASSERT(serv_obj != NULL);

	nbp = bufferpools_tlist_length(&serv_obj->rios_buffer_pools);

	return nbp;
}

static int check_buffer_pool_per_domain(char *cs_argv[], int cs_argc, int nbp)
{
	int	rc;
	int	bp_count;

	C2_RPC_SERVER_CTX_DECLARE(sctx, cs_xprts, ARRAY_SIZE(cs_xprts),
				  cs_argv, cs_argc, SERVER_LOG_FILE_NAME);

	rc = c2_rpc_server_start(&sctx);
	C2_UT_ASSERT(rc == 0);

	bp_count = get_ioservice_buffer_pool_count(&sctx);
	C2_UT_ASSERT(bp_count == nbp);

	c2_rpc_server_stop(&sctx);

	return rc;
}

void test_ios_bp_single_dom()
{
	/* It will create single buffer pool (per domain)*/
	check_buffer_pool_per_domain(ios_ut_bp_singledom_cmd,
				     ARRAY_SIZE(ios_ut_bp_singledom_cmd), 1);
}

void test_ios_bp_multi_dom()
{
	/* It will create two buffer pool (per domain) */
	check_buffer_pool_per_domain(ios_ut_bp_multidom_cmd,
				     ARRAY_SIZE(ios_ut_bp_multidom_cmd), 2);
}

void test_ios_bp_repeat_dom()
{
	/* It will create single buffer pool (per domain) */
	check_buffer_pool_per_domain(ios_ut_bp_repeatdom_cmd,
				     ARRAY_SIZE(ios_ut_bp_repeatdom_cmd), 1);
}
void test_ios_bp_onerepeat_dom()
{
	/* It will create two buffer pool (per domain) */
	check_buffer_pool_per_domain(ios_ut_bp_onerepeatdom_cmd,
				     ARRAY_SIZE(ios_ut_bp_onerepeatdom_cmd), 2);
}

const struct c2_test_suite ios_bufferpool_ut = {
        .ts_name = "ios-bufferpool-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "ios-bufferpool-single-domain", test_ios_bp_single_dom},
                { "ios-bufferpool-multiple-domains", test_ios_bp_multi_dom},
                { "ios-bufferpool-repeat-domains", test_ios_bp_repeat_dom},
                { "ios-bufferpool-onerepeat-domain", test_ios_bp_onerepeat_dom},
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
