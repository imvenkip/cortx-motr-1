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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 08/06/2011
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "lib/ut.h"
#include "lib/memory.h"
#include "lib/processor.h"
#include "fop/fop.h"
#include "reqh/reqh.h"
#include "rpc/rpclib.h"
#include "net/lnet/lnet.h"
#include "ut/rpc.h"

#include "fop/ut/long_lock/rdwr_fop.h"
#include "fop/ut/long_lock/rdwr_fom.h"
#include "fop/ut/long_lock/rdwr_fop_u.h"
#include "fop/ut/long_lock/rdwr_test_bench.h"

#define LOG_FILE_NAME		"fom_lock_ut.log"
#define SERVER_ENDPOINT_ADDR	"0@lo:12345:34:1"
#define SERVER_ENDPOINT		"lnet:" SERVER_ENDPOINT_ADDR
#define SERVER_DB_FILE_NAME	"fom_lock_ut_server.db"
#define SERVER_STOB_FILE_NAME	"fom_lock_ut_server.stob"

char *server_argv[] = {
	"fom_lock_lib_ut", "-r", "-T", "AD",
	"-D", SERVER_DB_FILE_NAME, "-S", SERVER_STOB_FILE_NAME,
	"-e", SERVER_ENDPOINT, "-s", "ds1", "-s", "ds2"
};

static struct c2_net_xprt    *xprt = &c2_net_lnet_xprt;
struct c2_net_domain	      client_net_dom = { };
static struct c2_colibri      colibri_ctx;
static FILE                  *log_file;

static void test_long_lock(void)
{
	struct c2_reqh *reqh = c2_cs_reqh_get(&colibri_ctx, "ds1");
	c2_rdwr_send_fop(reqh);
}

static int test_long_lock_init(void)
{
	int rc;
	size_t i;

	rc = c2_rdwr_fop_init();
	C2_ASSERT(rc == 0);

	rc = c2_net_xprt_init(xprt);
	C2_ASSERT(rc == 0);

	rc = c2_net_domain_init(&client_net_dom, xprt);
	C2_ASSERT(rc == 0);

	log_file = fopen(LOG_FILE_NAME, "w+");
	C2_ASSERT(log_file != NULL);

	for (i = 0; i < cs_default_stypes_nr; ++i) {
		rc = c2_reqh_service_type_register(cs_default_stypes[i]);
		C2_ASSERT(rc == 0);
	}

	rc = c2_cs_init(&colibri_ctx, &xprt, 1, log_file);
	C2_ASSERT(rc == 0);

	rc = c2_cs_setup_env(&colibri_ctx, ARRAY_SIZE(server_argv), server_argv);
	C2_ASSERT(rc == 0);

	rc = c2_cs_start(&colibri_ctx);
	C2_ASSERT(rc == 0);

	return rc;
}

static int test_long_lock_fini(void)
{
	size_t i;

	c2_cs_fini(&colibri_ctx);

	for (i = 0; i < cs_default_stypes_nr; ++i)
		c2_reqh_service_type_unregister(cs_default_stypes[i]);

	fclose(log_file);
	c2_net_domain_fini(&client_net_dom);
	c2_net_xprt_fini(xprt);
	c2_rdwr_fop_fini();

	return 0;
}

const struct c2_test_suite fop_lock_ut = {
	.ts_name = "fop-lock-ut",
	.ts_init = test_long_lock_init,
	.ts_fini = test_long_lock_fini,
	.ts_tests = {
		{ "fop-lock", test_long_lock },
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
