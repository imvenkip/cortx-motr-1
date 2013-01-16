/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
#include "mero/setup.h"
#include "lib/finject.h"
#include "cob/cob.h"
#include "mdstore/mdstore.h" /* m0_cob_alloc(), m0_cob_nskey_make(),
				m0_cob_fabrec_make(), m0_cob_create */

#include "fop/fom.h" /* M0_FSO_AGAIN, M0_FSO_WAIT */
#include "ioservice/io_service.h"
#include "sns/cm/ut/cp_common.h"

#include "ioservice/io_service.h"
#include "sns/cm/ut/cp_common.h"
#include "sns/cm/cm.h"
#include "sns/cm/cp.h"
#include "sns/cm/ag.h"

enum {
	ITER_UT_BUF_NR = 1 << 4,
	ITER_GOB_KEY_START = 4,
	GOB_NR = 5
};


static struct m0_reqh    *reqh;
struct m0_reqh_service   *service;
static struct m0_cm      *cm;
static struct m0_sns_cm  *scm;

static void service_start_success(void)
{
	int rc;

        rc = sns_cm_ut_server_start();
        M0_UT_ASSERT(rc == 0);
	sns_cm_ut_server_stop();
}

static void service_init_failure(void)
{
	int rc;

	m0_fi_enable_once("m0_cm_init", "init_failure");
	rc = sns_cm_ut_server_start();
	M0_UT_ASSERT(rc != 0);
}

static void service_start_failure(void)
{
	int rc;

	m0_fi_enable_once("m0_cm_setup", "setup_failure");
	rc = sns_cm_ut_server_start();
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

	rc = sns_cm_ut_server_start();
	M0_UT_ASSERT(rc == 0);

	reqh = m0_cs_reqh_get(&sctx, "sns_cm");
	M0_UT_ASSERT(reqh != NULL);
	service = m0_reqh_service_find(m0_reqh_service_type_find("sns_cm"),
				       reqh);
	M0_UT_ASSERT(service != NULL);

	cm = container_of(service, struct m0_cm, cm_service);
	scm = cm2sns(cm);

	bufs_nr = cm_buffer_pool_provision(&scm->sc_ibp, ITER_UT_BUF_NR);
	M0_UT_ASSERT(bufs_nr != 0);
	bufs_nr = cm_buffer_pool_provision(&scm->sc_obp, ITER_UT_BUF_NR);
	M0_UT_ASSERT(bufs_nr != 0);
        scm->sc_it.si_pl.spl_N = N;
        scm->sc_it.si_pl.spl_K = K;
        scm->sc_it.si_pl.spl_P = P;
	rc = m0_sns_cm_iter_init(&scm->sc_it);
	M0_UT_ASSERT(rc == 0);
}


static bool cp_verify(struct m0_sns_cm_cp *scp)
{
	return m0_stob_id_is_set(&scp->sc_sid) && scp->sc_base.c_ag != NULL &&
	       scp->sc_base.c_data != NULL;
}

static void dbenv_cob_domain_get(struct m0_dbenv **dbenv,
				 struct m0_cob_domain **cdom)
{
	*dbenv = cm->cm_service.rs_reqh->rh_dbenv;
	M0_UT_ASSERT(dbenv != NULL);
	M0_UT_ASSERT(m0_ios_cdom_get(cm->cm_service.rs_reqh, cdom, 0) == 0);
}

static void cob_create(uint64_t cont, struct m0_fid *gfid, uint32_t cob_idx)
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
	int                   rc;

	M0_SET0(&nsrec);
	M0_SET0(&omgrec);
	dbenv_cob_domain_get(&dbenv, &cdom);
	rc = m0_cob_alloc(cdom, &cob);
	M0_UT_ASSERT(rc == 0 && cob != NULL);
	m0_fid_set(&cob_fid, cont, gfid->f_key);

	M0_SET_ARR0(nskey_bs);
        snprintf((char*)nskey_bs, UINT32_MAX_STR_LEN, "%u",
                 (uint32_t)cob_idx);
        nskey_bs_len = strlen(nskey_bs);

	rc = m0_cob_nskey_make(&nskey, gfid, (char *)nskey_bs, nskey_bs_len);
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

static void buf_put(struct m0_sns_cm_cp *scp)
{
	m0_sns_cm_buffer_put(&scm->sc_obp, container_of(scp->sc_base.c_data,
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
	struct m0_fid gfid;
	uint32_t      cob_idx;
	int           i;
	int           j;

	gfid.f_container = 0;
	for (i = 0; i < GOB_NR; ++i) {
		gfid.f_key = ITER_GOB_KEY_START + i;
		cob_idx = 0;
		for (j = 1; j <= nr_cobs; ++j) {
			cob_create(j, &gfid, cob_idx);
			cob_idx++;
		}
	}
}

static void cobs_delete(uint64_t nr_cobs)
{
	int i;
	int j;

	for (i = 0; i < GOB_NR; ++i) {
		for (j = 1; j <= nr_cobs; ++j)
			cob_delete(j, ITER_GOB_KEY_START + i);
	}
}

static void nsit_verify(struct m0_fid *gfid, int cnt)
{
	M0_UT_ASSERT(gfid->f_container == 0);
	/* Verify that gob-fid has been enumerated in lexicographical order. */
	M0_UT_ASSERT(gfid->f_key - cnt == ITER_GOB_KEY_START);
}

static int iter_run(uint64_t pool_width, uint64_t fsize, uint64_t fdata,
		    bool verify_ns_iter, enum m0_sns_cm_op op)
{
	struct m0_sns_cm_cp scp;
	int                 rc;
	int                 cnt;

	cobs_create(pool_width);
	/* Set file size */
	scm->sc_it.si_pl.spl_fsize = fsize;
	/* Set fail device. */
	scm->sc_it.si_fdata = fdata;
	scm->sc_op = op;
	cnt = 0;
	m0_cm_lock(cm);
	do {
		M0_SET0(&scp);
		scm->sc_it.si_cp = &scp;
		rc = m0_sns_cm_iter_next(cm, &scp.sc_base);
		if (rc == M0_FSO_AGAIN) {
			if (verify_ns_iter)
				nsit_verify(&scm->sc_it.si_pl.spl_gob_fid, cnt);
			M0_UT_ASSERT(cp_verify(&scp));
			buf_put(&scp);
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
	m0_sns_cm_iter_fini(&scm->sc_it);
	/* Destroy previously created aggregation groups manually. */
	ag_destroy();
	m0_cm_unlock(cm);
	sns_cm_ut_server_stop();
}

static void iter_repair_success(void)
{
	int      rc;

	iter_setup(3, 1, 5);
	rc = iter_run(5, 36864, 4, true, SNS_REPAIR);
	M0_UT_ASSERT(rc == -ENODATA);
	iter_stop(5);
}

static void iter_rebalance_success(void)
{
	int      rc;

	iter_setup(3, 1, 5);
	rc = iter_run(5, 36864, 4, true, SNS_REBALANCE);
	M0_UT_ASSERT(rc == -ENODATA);
	iter_stop(5);
}

static void iter_pool_width_more_than_N_plus_2K(void)
{
	int rc;

	iter_setup(2, 1, 10);
	rc = iter_run(10, 36864, 4, false, SNS_REPAIR);
	M0_UT_ASSERT(rc == -ENODATA);
	iter_stop(10);
}

static void iter_invalid_nr_cobs(void)
{
	int rc;

	iter_setup(3, 1, 5);
	rc = iter_run(3, 36864, 4, false, SNS_REPAIR);
	M0_UT_ASSERT(rc == -ENODATA);
	iter_stop(3);
}

const struct m0_test_suite sns_cm_ut = {
	.ts_name = "sns-cm-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "service-startstop", service_start_success},
		{ "service-init-fail", service_init_failure},
		{ "service-start-fail", service_start_failure},
		{ "iter-repair-success", iter_repair_success},
		{ "iter-rebalance-success", iter_rebalance_success},
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
