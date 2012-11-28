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

#include "lib/ut.h"    /* M0_UT_ASSERT */
#include "lib/errno.h"
#include "lib/tlist.h"

#include "ut/rpc.h"
#include "rpc/rpclib.h"
#include "net/lnet/lnet.h"
#include "net/bulk_mem.h"
#include "reqh/reqh_service.h"
#include "ioservice/io_service.h"

#include "mero/mero_setup.h"
#include "ioservice/io_service.c"

struct m0_reqh *m0_cs_reqh_get(struct m0_mero *cctx,
			       const char *service_name);

extern const struct m0_tl_descr bufferpools_tl;
extern const struct m0_tl_descr m0_rhctx_tl;

 /* Mero setup arguments. */
static char *ios_ut_bp_singledom_cmd[] = { "mero_setup", "-r", "-p", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "lnet:0@lo:12345:34:1",
                                "-s", "ioservice"};
static char *ios_ut_bp_multidom_cmd[] = { "mero_setup", "-r", "-p", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "lnet:0@lo:12345:34:1",
                                "-e", "bulk-mem:127.0.0.1:35678",
                                "-s", "ioservice"};
static char *ios_ut_bp_repeatdom_cmd[] = { "mero_setup", "-r", "-p", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "bulk-mem:127.0.0.1:35678",
                                "-e", "bulk-mem:127.0.0.1:35679",
                                "-s", "ioservice"};
static char *ios_ut_bp_onerepeatdom_cmd[] = { "mero_setup", "-r", "-p", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "lnet:0@lo:12345:35:1",
                                "-e", "bulk-mem:127.0.0.1:35678",
                                "-e", "bulk-mem:127.0.0.1:35679",
                                "-s", "ioservice"};
/*
  Transports used in mero a context.
 */
static struct m0_net_xprt *cs_xprts[] = {
	&m0_net_lnet_xprt,
	&m0_net_bulk_mem_xprt
};

#define SERVER_LOG_FILE_NAME	"cs_ut.errlog"

static int get_ioservice_buffer_pool_count(struct m0_rpc_server_ctx *sctx)
{
	struct m0_reqh_io_service *serv_obj;
	struct m0_reqh_service    *reqh_ios;
	struct m0_reqh            *reqh;

	reqh     = m0_cs_reqh_get(&sctx->rsx_mero_ctx, "ioservice");
	reqh_ios = m0_reqh_service_find(&m0_ios_type, reqh);
	serv_obj = container_of(reqh_ios, struct m0_reqh_io_service, rios_gen);

	M0_UT_ASSERT(serv_obj != NULL);

	return bufferpools_tlist_length(&serv_obj->rios_buffer_pools);
}

static int check_buffer_pool_per_domain(char *cs_argv[], int cs_argc, int nbp)
{
	int rc;
	int bp_count;

	M0_RPC_SERVER_CTX_DEFINE(sctx, cs_xprts, ARRAY_SIZE(cs_xprts),
				 cs_argv, cs_argc, m0_cs_default_stypes,
				 m0_cs_default_stypes_nr, SERVER_LOG_FILE_NAME);

	rc = m0_rpc_server_start(&sctx);
	M0_UT_ASSERT(rc == 0);

	bp_count = get_ioservice_buffer_pool_count(&sctx);
	M0_UT_ASSERT(bp_count == nbp);

	m0_rpc_server_stop(&sctx);

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

const struct m0_test_suite ios_bufferpool_ut = {
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
