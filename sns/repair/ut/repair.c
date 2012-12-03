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

#include <sys/stat.h>

#include "lib/ut.h"
#include "lib/misc.h"
#include "lib/memory.h"

#include "net/buffer_pool.h"
#include "net/lnet/lnet.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "colibri/colibri_setup.h"
#include "lib/finject.h"
#include "cob/cob.h"
#include "mdstore/mdstore.h" /* c2_cob_alloc(), c2_cob_nskey_make(),
				c2_cob_fabrec_make(), c2_cob_create */

#include "fop/fom.h" /* C2_FSO_AGAIN, C2_FSO_WAIT */

#include "sns/repair/cm.h"
#include "sns/repair/cp.h"
#include "sns/repair/ag.h"


#define LOG_FILE_NAME "sr_ut.errlog"

static char *sns_repair_ut_svc[] = { "colibri_setup", "-r", "-p", "-T", "linux",
                                "-D", "sr_db", "-S", "sr_stob",
                                "-e", "lnet:0@lo:12345:34:1" ,
                                "-s", "sns_repair"};

static struct c2_net_xprt *sr_xprts[] = {
        &c2_net_lnet_xprt,
};

enum {
	ITER_UT_BUF_NR = 1 << 4,
	ITER_DEFAULT_COB_FID_CONT = 4
};


static struct c2_colibri        sctx;
static struct c2_reqh          *reqh;
struct c2_reqh_service         *service;
static struct c2_cm            *cm;
static struct c2_sns_repair_cm *rcm;
static FILE                    *lfile;

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
	if (rc == 0)
		rc = c2_cs_start(&sctx);
        if (rc != 0)
		server_stop();

	return rc;
}

static void service_start_success(void)
{
	int rc;

        rc = server_start();
        C2_UT_ASSERT(rc == 0);
	server_stop();
}

static void service_init_failure(void)
{
	int rc;

	c2_fi_enable_once("c2_cm_init", "init_failure");
	rc = server_start();
	C2_UT_ASSERT(rc != 0);
}

static void service_start_failure(void)
{
	int rc;

	c2_fi_enable_once("c2_cm_setup", "setup_failure");
	rc = server_start();
	C2_UT_ASSERT(rc != 0);
}

static size_t cm_buffer_pool_provision(struct c2_net_buffer_pool *bp,
                                       size_t bufs_nr)
{
	size_t bnr;

	c2_net_buffer_pool_lock(bp);
	C2_PRE(c2_net_buffer_pool_invariant(bp));
	bnr = c2_net_buffer_pool_provision(bp, bufs_nr);
	c2_net_buffer_pool_unlock(bp);

	return bnr;
}

static void iter_setup(uint32_t N, uint32_t K, uint32_t P)
{
	size_t bufs_nr;
	int    rc;

	rc = server_start();
	C2_UT_ASSERT(rc == 0);

	reqh = c2_cs_reqh_get(&sctx, "sns_repair");
	C2_UT_ASSERT(reqh != NULL);
	service = c2_reqh_service_find(c2_reqh_service_type_find("sns_repair"),
				       reqh);
	C2_UT_ASSERT(service != NULL);

	cm = container_of(service, struct c2_cm, cm_service);
	rcm = cm2sns(cm);

	bufs_nr = cm_buffer_pool_provision(&rcm->rc_ibp, ITER_UT_BUF_NR);
	C2_UT_ASSERT(bufs_nr != 0);
	bufs_nr = cm_buffer_pool_provision(&rcm->rc_obp, ITER_UT_BUF_NR);
	C2_UT_ASSERT(bufs_nr != 0);
        rcm->rc_it.ri_pl.rpl_N = N;
        rcm->rc_it.ri_pl.rpl_K = K;
        rcm->rc_it.ri_pl.rpl_P = P;
	rc = c2_sns_repair_iter_init(rcm);
	C2_UT_ASSERT(rc == 0);
}


static bool cp_verify(struct c2_sns_repair_cp *rcp)
{
	return c2_stob_id_is_set(&rcp->rc_sid) && rcp->rc_base.c_ag != NULL &&
	       rcp->rc_base.c_data != NULL;
}

static void dbenv_cob_domain_get(struct c2_dbenv **dbenv,
				 struct c2_cob_domain **cdom)
{
	*dbenv = cm->cm_service.rs_reqh->rh_dbenv;
	C2_UT_ASSERT(dbenv != NULL);
	*cdom = &cm->cm_service.rs_reqh->rh_mdstore->md_dom;
	C2_UT_ASSERT(cdom != NULL);

}

static void cob_create(uint64_t cont, uint64_t key)
{
	struct c2_cob        *cob;
	struct c2_cob_domain *cdom;
	struct c2_fid         cob_fid;
	struct c2_dtx         tx;
	struct c2_dbenv      *dbenv;
	struct c2_cob_nskey  *nskey;
	struct c2_cob_nsrec   nsrec;
	struct c2_cob_fabrec *fabrec;
	struct c2_cob_omgrec  omgrec;
        char                  nskey_bs[UINT32_MAX_STR_LEN];
        uint32_t              nskey_bs_len;
        uint32_t              cob_idx = 1;
	int                   rc;

	C2_SET0(&nsrec);
	C2_SET0(&omgrec);
	dbenv_cob_domain_get(&dbenv, &cdom);
	rc = c2_cob_alloc(cdom, &cob);
	C2_UT_ASSERT(rc == 0 && cob != NULL);
	c2_fid_set(&cob_fid, cont, key);

	C2_SET_ARR0(nskey_bs);
        snprintf((char*)nskey_bs, UINT32_MAX_STR_LEN, "%u",
                 (uint32_t)cob_idx);
        nskey_bs_len = strlen(nskey_bs);

	rc = c2_cob_nskey_make(&nskey, &cob_fid, (char *)nskey_bs,
			       nskey_bs_len);
	C2_UT_ASSERT(rc == 0 && nskey != NULL);
	nsrec.cnr_fid = cob_fid;
	nsrec.cnr_nlink = 1;

	rc = c2_cob_fabrec_make(&fabrec, NULL, 0);
	C2_UT_ASSERT(rc == 0 && fabrec != NULL);
	rc = c2_db_tx_init(&tx.tx_dbtx, dbenv, 0);
	C2_UT_ASSERT(rc == 0);
	rc = c2_cob_create(cob, nskey, &nsrec, fabrec, &omgrec, &tx.tx_dbtx);
	C2_UT_ASSERT(rc == 0);
	c2_db_tx_commit(&tx.tx_dbtx);
	c2_cob_put(cob);
}

static void cob_delete(uint64_t cont, uint64_t key)
{
	struct c2_cob        *cob;
	struct c2_cob_domain *cdom;
	struct c2_fid         cob_fid;
	struct c2_dtx         tx;
	struct c2_dbenv      *dbenv;
	struct c2_cob_oikey   oikey;
	int                   rc;

	dbenv_cob_domain_get(&dbenv, &cdom);
	c2_fid_set(&cob_fid, cont, key);
	c2_cob_oikey_make(&oikey, &cob_fid, 0);
	rc = c2_db_tx_init(&tx.tx_dbtx, dbenv, 0);
	C2_UT_ASSERT(rc == 0);
	rc = c2_cob_locate(cdom, &oikey, 0, &cob, &tx.tx_dbtx);
	C2_UT_ASSERT(rc == 0);
	rc = c2_cob_delete_put(cob, &tx.tx_dbtx);
	C2_UT_ASSERT(rc == 0);
	c2_db_tx_commit(&tx.tx_dbtx);
}

static void buf_put(struct c2_sns_repair_cp *rcp)
{
	c2_sns_repair_buffer_put(&rcm->rc_obp, container_of(rcp->rc_base.c_data,
							    struct c2_net_buffer,
							    nb_buffer),
				 0);
}

static void ag_destroy(void)
{
	struct c2_cm_aggr_group *ag;

	c2_tl_for(aggr_grps, &cm->cm_aggr_grps, ag) {
		ag->cag_ops->cago_fini(ag);
	} c2_tl_endfor;
}

static void cobs_create(uint64_t nr_cobs)
{
	int i;

	for (i = 5; i <= 10; ++i)
		cob_create(ITER_DEFAULT_COB_FID_CONT, i);

}

static void cobs_delete(uint64_t nr_cobs)
{
	int i;

	for (i = 5; i <= 10; ++i)
		cob_delete(ITER_DEFAULT_COB_FID_CONT, i);
}

static int iter_run(uint64_t pool_width, uint64_t fsize, uint64_t fdata)
{
	struct c2_sns_repair_cp rcp;
	int                     rc;

	cobs_create(pool_width);
	/* Set file size */
	rcm->rc_file_size = fsize;
	/* Set fail device. */
	rcm->rc_fdata = fdata;
	c2_cm_lock(cm);
	do {
		C2_SET0(&rcp);
		rcm->rc_it.ri_cp = &rcp;
		rc = c2_sns_repair_iter_next(cm, &rcp.rc_base);
		if (rc == C2_FSO_AGAIN) {
			C2_UT_ASSERT(cp_verify(&rcp));
			buf_put(&rcp);
		}
	} while (rc == C2_FSO_AGAIN);
	c2_cm_unlock(cm);

	return rc;
}

static void iter_stop(uint64_t pool_width)
{
	cobs_delete(pool_width);
	c2_cm_lock(cm);
	c2_sns_repair_iter_fini(rcm);
	/* Destroy previously created aggregation groups manually. */
	ag_destroy();
	c2_cm_unlock(cm);
	server_stop();
}

static void iter_success(void)
{
	int      rc;

	iter_setup(3, 1, 5);
	rc = iter_run(5, 36864, 2);
	C2_UT_ASSERT(rc == -ENODATA);
	iter_stop(5);
}

static void iter_pool_width_more_than_N_plus_2K(void)
{
	int rc;

	iter_setup(2, 1, 10);
	rc = iter_run(10, 36864, 2);
	C2_UT_ASSERT(rc == -ENODATA);
	iter_stop(10);
}

static void iter_invalid_nr_cobs(void)
{
	int rc;

	iter_setup(3, 1, 5);
	rc = iter_run(3, 36864, 2);
	C2_UT_ASSERT(rc == -ENODATA);
	iter_stop(3);
}

const struct c2_test_suite sns_repair_ut = {
	.ts_name = "sns-repair-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "service-startstop", service_start_success},
		{ "service-init-fail", service_init_failure},
		{ "service-start-fail", service_start_failure},
		{ "iter-success", iter_success},
		{ "iter-pool-width-more-than-N-plus-2K",
		  iter_pool_width_more_than_N_plus_2K},
		{ "iter-invalid-nr-cobs", iter_invalid_nr_cobs},
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
