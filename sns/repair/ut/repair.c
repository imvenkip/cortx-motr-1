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
#include "mero/mero_setup.h"
#include "lib/finject.h"
#include "cob/cob.h"
#include "mdstore/mdstore.h" /* m0_cob_alloc(), m0_cob_nskey_make(),
				m0_cob_fabrec_make(), m0_cob_create */

#include "fop/fom.h" /* M0_FSO_AGAIN, M0_FSO_WAIT */

#include "sns/repair/cm.h"
#include "sns/repair/cp.h"
#include "sns/repair/ag.h"


#define LOG_FILE_NAME "sr_ut.errlog"

static char *sns_repair_ut_svc[] = { "mero_setup", "-r", "-p", "-T", "linux",
                                "-D", "sr_db", "-S", "sr_stob",
                                "-e", "lnet:0@lo:12345:34:1" ,
                                "-s", "sns_repair"};

static struct m0_net_xprt *sr_xprts[] = {
        &m0_net_lnet_xprt,
};

enum {
	ITER_UT_BUF_NR = 1 << 4,
	ITER_DEFAULT_COB_FID_KEY = 4
};


static struct m0_mero        sctx;
static struct m0_reqh          *reqh;
struct m0_reqh_service         *service;
static struct m0_cm            *cm;
static struct m0_sns_repair_cm *rcm;
static FILE                    *lfile;

static void server_stop(void)
{
	m0_cs_fini(&sctx);
	fclose(lfile);
}

static int server_start(void)
{
	int rc;

	M0_SET0(&sctx);
	lfile = fopen(LOG_FILE_NAME, "w+");
	M0_UT_ASSERT(lfile != NULL);

        rc = m0_cs_init(&sctx, sr_xprts, ARRAY_SIZE(sr_xprts), lfile);
        if (rc != 0)
		return rc;

        rc = m0_cs_setup_env(&sctx, ARRAY_SIZE(sns_repair_ut_svc),
                             sns_repair_ut_svc);
	if (rc == 0)
		rc = m0_cs_start(&sctx);
        if (rc != 0)
		server_stop();

	return rc;
}

static void service_start_success(void)
{
	int rc;

        rc = server_start();
        M0_UT_ASSERT(rc == 0);
	server_stop();
}

static void service_init_failure(void)
{
	int rc;

	m0_fi_enable_once("m0_cm_init", "init_failure");
	rc = server_start();
	M0_UT_ASSERT(rc != 0);
}

static void service_start_failure(void)
{
	int rc;

	m0_fi_enable_once("m0_cm_setup", "setup_failure");
	rc = server_start();
	M0_UT_ASSERT(rc != 0);
}

static size_t cm_buffer_pool_provision(struct m0_net_buffer_pool *bp,
                                       size_t bufs_nr)
{
	size_t bnr;

	m0_net_buffer_pool_lock(bp);
	M0_PRE(m0_net_buffer_pool_invariant(bp));
	bnr = m0_net_buffer_pool_provision(bp, bufs_nr);
	m0_net_buffer_pool_unlock(bp);

	return bnr;
}

static void iter_setup(uint32_t N, uint32_t K, uint32_t P)
{
	size_t bufs_nr;
	int    rc;

	rc = server_start();
	M0_UT_ASSERT(rc == 0);

	reqh = m0_cs_reqh_get(&sctx, "sns_repair");
	M0_UT_ASSERT(reqh != NULL);
	service = m0_reqh_service_find(m0_reqh_service_type_find("sns_repair"),
				       reqh);
	M0_UT_ASSERT(service != NULL);

	cm = container_of(service, struct m0_cm, cm_service);
	rcm = cm2sns(cm);

	bufs_nr = cm_buffer_pool_provision(&rcm->rc_ibp, ITER_UT_BUF_NR);
	M0_UT_ASSERT(bufs_nr != 0);
	bufs_nr = cm_buffer_pool_provision(&rcm->rc_obp, ITER_UT_BUF_NR);
	M0_UT_ASSERT(bufs_nr != 0);
        rcm->rc_it.ri_pl.rpl_N = N;
        rcm->rc_it.ri_pl.rpl_K = K;
        rcm->rc_it.ri_pl.rpl_P = P;
	rc = m0_sns_repair_iter_init(rcm);
	M0_UT_ASSERT(rc == 0);
}


static bool cp_verify(struct m0_sns_repair_cp *rcp)
{
	return m0_stob_id_is_set(&rcp->rc_sid) && rcp->rc_base.c_ag != NULL &&
	       rcp->rc_base.c_data != NULL;
}

static void dbenv_cob_domain_get(struct m0_dbenv **dbenv,
				 struct m0_cob_domain **cdom)
{
	*dbenv = cm->cm_service.rs_reqh->rh_dbenv;
	M0_UT_ASSERT(dbenv != NULL);
	*cdom = &cm->cm_service.rs_reqh->rh_mdstore->md_dom;
	M0_UT_ASSERT(cdom != NULL);

}

static void cob_create(uint64_t cont, uint64_t key)
{
	struct m0_cob        *cob;
	struct m0_cob_domain *cdom;
	struct m0_fid         cob_fid;
	struct m0_dtx         tx;
	struct m0_dbenv      *dbenv;
	struct m0_cob_nskey  *nskey;
	struct m0_cob_nsrec   nsrec;
	struct m0_cob_fabrec *fabrec;
	struct m0_cob_omgrec  omgrec;
        char                  nskey_bs[UINT32_MAX_STR_LEN];
        uint32_t              nskey_bs_len;
        uint32_t              cob_idx = 1;
	int                   rc;

	M0_SET0(&nsrec);
	M0_SET0(&omgrec);
	dbenv_cob_domain_get(&dbenv, &cdom);
	rc = m0_cob_alloc(cdom, &cob);
	M0_UT_ASSERT(rc == 0 && cob != NULL);
	m0_fid_set(&cob_fid, cont, key);

	M0_SET_ARR0(nskey_bs);
        snprintf((char*)nskey_bs, UINT32_MAX_STR_LEN, "%u",
                 (uint32_t)cob_idx);
        nskey_bs_len = strlen(nskey_bs);

	rc = m0_cob_nskey_make(&nskey, &cob_fid, (char *)nskey_bs,
			       nskey_bs_len);
	M0_UT_ASSERT(rc == 0 && nskey != NULL);
	nsrec.cnr_fid = cob_fid;
	nsrec.cnr_nlink = 1;

	rc = m0_cob_fabrec_make(&fabrec, NULL, 0);
	M0_UT_ASSERT(rc == 0 && fabrec != NULL);
	rc = m0_db_tx_init(&tx.tx_dbtx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);
	rc = m0_cob_create(cob, nskey, &nsrec, fabrec, &omgrec, &tx.tx_dbtx);
	M0_UT_ASSERT(rc == 0);
	m0_db_tx_commit(&tx.tx_dbtx);
	m0_cob_put(cob);
}

static void cob_delete(uint64_t cont, uint64_t key)
{
	struct m0_cob        *cob;
	struct m0_cob_domain *cdom;
	struct m0_fid         cob_fid;
	struct m0_dtx         tx;
	struct m0_dbenv      *dbenv;
	struct m0_cob_oikey   oikey;
	int                   rc;

	dbenv_cob_domain_get(&dbenv, &cdom);
	m0_fid_set(&cob_fid, cont, key);
	m0_cob_oikey_make(&oikey, &cob_fid, 0);
	rc = m0_db_tx_init(&tx.tx_dbtx, dbenv, 0);
	M0_UT_ASSERT(rc == 0);
	rc = m0_cob_locate(cdom, &oikey, 0, &cob, &tx.tx_dbtx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_cob_delete_put(cob, &tx.tx_dbtx);
	M0_UT_ASSERT(rc == 0);
	m0_db_tx_commit(&tx.tx_dbtx);
}

static void buf_put(struct m0_sns_repair_cp *rcp)
{
	m0_sns_repair_buffer_put(&rcm->rc_obp, container_of(rcp->rc_base.c_data,
							    struct m0_net_buffer,
							    nb_buffer),
				 0);
}

static void ag_destroy(void)
{
	struct m0_cm_aggr_group *ag;

	m0_tl_for(aggr_grps, &cm->cm_aggr_grps, ag) {
		ag->cag_ops->cago_fini(ag);
	} m0_tl_endfor;
}

static void cobs_create(uint64_t nr_cobs)
{
	int i;

        for (i = 1; i <= nr_cobs; ++i)
                cob_create(i, ITER_DEFAULT_COB_FID_KEY);
}

static void cobs_delete(uint64_t nr_cobs)
{
	int i;

        for (i = 1; i <= nr_cobs; ++i)
		cob_delete(i, ITER_DEFAULT_COB_FID_KEY);
}

static void nsit_verify(struct m0_fid *gfid, int cnt)
{
	M0_UT_ASSERT(gfid->f_container == 0);
	/* Verify that gob-fid has been enumerated in lexicographical order. */
	M0_UT_ASSERT(gfid->f_key - cnt == ITER_DEFAULT_COB_FID_KEY);
}

static int iter_run(uint64_t pool_width, uint64_t fsize, uint64_t fdata,
		    bool verify_ns_iter)
{
	struct m0_sns_repair_cp rcp;
	int                     rc;
	int                     cnt;

	cobs_create(pool_width);
	/* Set file size */
	rcm->rc_file_size = fsize;
	/* Set fail device. */
	rcm->rc_fdata = fdata;
	cnt = 0;
	m0_cm_lock(cm);
	do {
		M0_SET0(&rcp);
		rcm->rc_it.ri_cp = &rcp;
		rc = m0_sns_repair_iter_next(cm, &rcp.rc_base);
		if (rc == M0_FSO_AGAIN) {
			if (verify_ns_iter)
				nsit_verify(&rcm->rc_it.ri_pl.rpl_gob_fid, cnt);
			M0_UT_ASSERT(cp_verify(&rcp));
			buf_put(&rcp);
			cnt++;
		}
	} while (rc == M0_FSO_AGAIN);
	m0_cm_unlock(cm);

	return rc;
}

static void iter_stop(uint64_t pool_width)
{
	cobs_delete(pool_width);
	m0_cm_lock(cm);
	m0_sns_repair_iter_fini(rcm);
	/* Destroy previously created aggregation groups manually. */
	ag_destroy();
	m0_cm_unlock(cm);
	server_stop();
}

static void iter_success(void)
{
	int      rc;

	iter_setup(3, 1, 5);
	rc = iter_run(5, 36864, 2, true);
	M0_UT_ASSERT(rc == -ENODATA);
	iter_stop(5);
}

static void iter_pool_width_more_than_N_plus_2K(void)
{
	int rc;

	iter_setup(2, 1, 10);
	rc = iter_run(10, 36864, 2, false);
	M0_UT_ASSERT(rc == -ENODATA);
	iter_stop(10);
}

static void iter_invalid_nr_cobs(void)
{
	int rc;

	iter_setup(3, 1, 5);
	rc = iter_run(3, 36864, 2, false);
	M0_UT_ASSERT(rc == -ENODATA);
	iter_stop(3);
}

const struct m0_test_suite sns_repair_ut = {
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
