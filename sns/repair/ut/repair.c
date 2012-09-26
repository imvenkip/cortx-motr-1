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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 09/25/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/ut.h"
#include "lib/misc.h"
#include "net/lnet/lnet.h"
#include "colibri/colibri_setup.h"
#include "lib/finject.h"

#define LOG_FILE_NAME "sr_ut.errlog"

static char *sns_repair_ut_svc[] = { "colibri_setup", "-r", "-T", "AD",
                                "-D", "sr_db", "-S", "sr_stob",
                                "-e", "lnet:0@lo:12345:34:1" ,
                                "-s", "sns_repair"};

static struct c2_net_xprt *sr_xprts[] = {
        &c2_net_lnet_xprt,
};

static struct c2_colibri  sctx;
static FILE              *lfile;

static void server_stop(void)
{
	c2_cs_fini(&sctx);
	fclose(lfile);
}

static int server_start(void)
{
	int rc;

	C2_SET0(&sctx);
	lfile = fopen(LOG_FILE_NAME, "w+");
	C2_UT_ASSERT(lfile != NULL);

        rc = c2_cs_init(&sctx, sr_xprts, ARRAY_SIZE(sr_xprts), lfile);
        if (rc != 0)
		return rc;

        rc = c2_cs_setup_env(&sctx, ARRAY_SIZE(sns_repair_ut_svc),
                             sns_repair_ut_svc);
        if (rc != 0) {
		server_stop();
		return rc;
	}

        rc = c2_cs_start(&sctx);

	return rc;
}

static void test_service_start_success(void)
{
	int rc;

        rc = server_start();
        C2_UT_ASSERT(rc == 0);
	server_stop();
}

static void test_service_init_failure(void)
{
	int rc;

	rc = server_start();
	C2_UT_ASSERT(rc == 0);
	server_stop();
}

const struct c2_test_suite sns_repair_ut = {
	.ts_name = "sns-repair-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "service-startstop", test_service_start_success},
		{ "service-init-fail", test_service_init_failure},
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
