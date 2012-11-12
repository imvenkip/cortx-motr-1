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
 *                  Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 10/31/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/stat.h>
#include <stdlib.h>

#include "lib/misc.h"
#include "lib/chan.h"
#include "lib/getopts.h"

#include "net/lnet/lnet.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "colibri/colibri_setup.h"
#include "colibri/init.h"

#include "fop/fom.h" /* C2_FSO_AGAIN, C2_FSO_WAIT */

#include "sns/repair/cm.h"

#define LOG_FILE_NAME "sr_ut.errlog"
#define MAX_PATH_LEN  1024

static struct c2_net_xprt *sr_xprts[] = {
        &c2_net_lnet_xprt,
};

static struct c2_colibri        sctx;
static struct c2_reqh          *reqh;
struct c2_reqh_service         *service;
static struct c2_cm            *cm;
static struct c2_sns_repair_cm *rcm;
static FILE                    *lfile;
static struct c2_chan           repair_wait;
char                           *dbpath;
char                           *stob_path;

static void server_stop(void)
{
	c2_cs_fini(&sctx);
	fclose(lfile);
}

static int server_start(void)
{
	int rc;
	char *sns_repair_ut_svc[] = { "colibri_setup", "-r", "-T", "linux",
				      "-D", dbpath, "-S", stob_path,
				      "-e", "lnet:0@lo:12345:34:1" ,
				      "-s", "sns_repair"};

	C2_SET0(&sctx);
	lfile = fopen(LOG_FILE_NAME, "w+");
	C2_ASSERT(lfile != NULL);

        rc = c2_cs_init(&sctx, sr_xprts, ARRAY_SIZE(sr_xprts), lfile);
        if (rc != 0)
		return rc;

        rc = c2_cs_setup_env(&sctx, ARRAY_SIZE(sns_repair_ut_svc),
                             sns_repair_ut_svc);
	if (rc == 0)
		rc = c2_cs_start(&sctx);
        if (rc != 0)
		server_stop();

	return rc;
}

static void sns_setup(void)
{
	int    rc;

	rc = server_start();
	C2_ASSERT(rc == 0);

	reqh = c2_cs_reqh_get(&sctx, "sns_repair");
	C2_ASSERT(reqh != NULL);
	service = c2_reqh_service_find(c2_reqh_service_type_find("sns_repair"),
				       reqh);
	C2_ASSERT(service != NULL);

	cm = container_of(service, struct c2_cm, cm_service);
	rcm = cm2sns(cm);

	c2_chan_init(&repair_wait);
}

int main(int argc, char *argv[])
{
	struct c2_clink link_wait;
	uint64_t        fdata;
	uint64_t        fsize;
	uint64_t        N = 0;
	uint64_t        K = 0;
	uint64_t        P = 0;
	int             rc;

	rc = C2_GETOPTS("repair", argc, argv,
		C2_FORMATARG('F', "Failure device", "%lu", &fdata),
		C2_FORMATARG('s', "File size", "%lu", &fsize),
		C2_FORMATARG('N', "Number of data units", "%lu", &N),
		C2_FORMATARG('K', "Number of parity units", "%lu", &K),
		C2_FORMATARG('P', "Total pool width", "%lu", &P),
		C2_STRINGARG('D', "db path",
			LAMBDA(void, (const char *str){
					dbpath = (char*)str;
				     })),
		C2_STRINGARG('S', "Stob path",
			LAMBDA(void, (const char *str){
					stob_path = (char*)str;
				     })),
                );
        if (rc != 0)
                return rc;

	C2_ASSERT(P >= N + 2 * K);
	rc = c2_init();
	C2_ASSERT(rc == 0);
	sns_setup();

	rcm->rc_fdata = fdata;
	rcm->rc_file_size = fsize;
	rcm->rc_it.ri_pl.rpl_N = N;
	rcm->rc_it.ri_pl.rpl_K = K;
	rcm->rc_it.ri_pl.rpl_P = P;

	c2_clink_init(&link_wait, NULL);
	c2_clink_add(&rcm->rc_stop_wait, &link_wait);

	rc = c2_cm_start(&rcm->rc_base);
	C2_ASSERT(rc == 0);

	c2_chan_wait(&link_wait);

	c2_cm_stop(&rcm->rc_base);

	server_stop();
	c2_fini();

	return rc;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
