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

#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/finject.h"
#include "lib/locality.h"

#include "net/buffer_pool.h"
#include "net/lnet/lnet.h"
#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "mero/setup.h"
#include "cob/cob.h"
#include "mdstore/mdstore.h" /* m0_cob_alloc(), m0_cob_nskey_make(),
				m0_cob_fabrec_make(), m0_cob_create */
#include "fop/fom.h" /* M0_FSO_AGAIN, M0_FSO_WAIT */
#include "ioservice/io_service.h"
#include "ioservice/io_device.h"
#include "pool/pool.h"

#include "sns/cm/repair/ag.h"
#include "sns/cm/cm.h"
#include "sns/cm/repair/ut/cp_common.h"

enum {
	ITER_UT_BUF_NR     = 1 << 8,
	ITER_GOB_KEY_START = 4,
};

static struct m0_reqh   *reqh;
static struct m0_reqh_service  *service;
static struct m0_cm     *cm;
static struct m0_sns_cm *scm;

static void service_start_success(void)
{
	int rc;

        rc = cs_init(&sctx);
        M0_ASSERT(rc == 0);
	cs_fini(&sctx);
}

static void service_init_failure(void)
{
	int rc;

	m0_fi_enable_once("m0_cm_init", "init_failure");
        rc = cs_init(&sctx);
        M0_ASSERT(rc != 0);
}

static void service_start_failure(void)
{
	int rc;

	m0_fi_enable_once("m0_cm_setup", "setup_failure");
        rc = cs_init(&sctx);
        M0_ASSERT(rc != 0);
}

static void pool_mach_transit(struct m0_poolmach *pm, uint64_t fd,
			      enum m0_pool_nd_state state)
{
	struct m0_pool_event   pme;
	int                    rc;
	struct m0_be_tx_credit cred = {};
	struct m0_be_tx        tx;
	struct m0_sm_group    *grp  = m0_locality0_get()->lo_grp;

	M0_SET0(&pme);
	pme.pe_type  = M0_POOL_DEVICE;
	pme.pe_index = fd;
	pme.pe_state = state;

	m0_sm_group_lock(grp);
	m0_be_tx_init(&tx, 0, reqh->rh_beseg->bs_domain, grp,
			      NULL, NULL, NULL, NULL);
	m0_poolmach_store_credit(pm, &cred);

	m0_be_tx_prep(&tx, &cred);
	rc = m0_be_tx_open_sync(&tx);
	M0_ASSERT(rc == 0);

	rc = m0_poolmach_state_transit(cm->cm_pm, &pme, &tx);
	m0_be_tx_close_sync(&tx);
	m0_be_tx_fini(&tx);
	M0_UT_ASSERT(rc == 0);
	m0_sm_group_unlock(grp);
}

static void iter_setup(enum m0_sns_cm_op op, uint64_t fd)
{
	int rc;

        rc = cs_init(&sctx);
        M0_ASSERT(rc == 0);

	reqh = m0_cs_reqh_get(&sctx);
	service = m0_reqh_service_find(m0_reqh_service_type_find("sns_repair"),
				       reqh);
	M0_UT_ASSERT(service != NULL);

	cm = container_of(service, struct m0_cm, cm_service);
	cm->cm_pm = m0_ios_poolmach_get(reqh);
	scm = cm2sns(cm);
	pool_mach_transit(cm->cm_pm, fd, M0_PNDS_FAILED);
	scm->sc_op = op;
	rc = cm->cm_ops->cmo_prepare(cm);
	M0_UT_ASSERT(rc == 0);
	rc = cm->cm_ops->cmo_start(cm);
	M0_UT_ASSERT(rc == 0);
}

static bool cp_verify(struct m0_sns_cm_cp *scp)
{
	return m0_fid_is_valid(&scp->sc_stob_fid) &&
	       scp->sc_base.c_ag != NULL &&
	       !cp_data_buf_tlist_is_empty(&scp->sc_base.c_buffers);
}

static void dbenv_cob_domain_get(struct m0_dbenv **dbenv,
				 struct m0_cob_domain **cdom)
{
	*dbenv = cm->cm_service.rs_reqh->rh_dbenv;
	M0_UT_ASSERT(dbenv != NULL);
	M0_UT_ASSERT(m0_ios_cdom_get(cm->cm_service.rs_reqh, cdom) == 0);
}

M0_INTERNAL void cob_create(struct m0_dbenv *dbenv, struct m0_cob_domain *cdom,
			    uint64_t cont, struct m0_fid *gfid, uint32_t cob_idx)
{
	struct m0_sm_group   *grp = m0_locality0_get()->lo_grp;
	struct m0_cob        *cob;
	struct m0_fid         cob_fid;
	struct m0_dtx         tx;
	struct m0_cob_nskey  *nskey;
	struct m0_cob_nsrec   nsrec;
	struct m0_cob_fabrec *fabrec;
	struct m0_cob_omgrec  omgrec;
        char                  nskey_bs[UINT32_MAX_STR_LEN];
        uint32_t              nskey_bs_len;
	int                   rc;

	M0_SET0(&nsrec);
	M0_SET0(&omgrec);
	rc = m0_cob_alloc(cdom, &cob);
	M0_ASSERT(rc == 0 && cob != NULL);
	m0_fid_set(&cob_fid, cont, gfid->f_key);

	M0_SET_ARR0(nskey_bs);
        snprintf((char*)nskey_bs, UINT32_MAX_STR_LEN, "%u",
                 (uint32_t)cob_idx);
        nskey_bs_len = strlen(nskey_bs);

	rc = m0_cob_nskey_make(&nskey, gfid, (char *)nskey_bs, nskey_bs_len);
	M0_ASSERT(rc == 0 && nskey != NULL);
	nsrec.cnr_fid = cob_fid;
	nsrec.cnr_nlink = 1;

	rc = m0_cob_fabrec_make(&fabrec, NULL, 0);
	M0_ASSERT(rc == 0 && fabrec != NULL);
	m0_sm_group_lock(grp);
	m0_dtx_init(&tx, dbenv->d_i.d_seg->bs_domain, grp);
	m0_cob_tx_credit(cob->co_dom, M0_COB_OP_CREATE, &tx.tx_betx_cred);
	rc = m0_dtx_open_sync(&tx);
	M0_ASSERT(rc == 0);
	rc = m0_cob_create(cob, nskey, &nsrec, fabrec, &omgrec, &tx.tx_betx);
	M0_ASSERT(rc == 0);
	m0_dtx_done_sync(&tx);
	m0_dtx_fini(&tx);
	m0_sm_group_unlock(grp);
	m0_cob_put(cob);
}

static void cob_delete(uint64_t cont, uint64_t key)
{
	struct m0_sm_group   *grp = m0_locality0_get()->lo_grp;
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
	rc = m0_cob_locate(cdom, &oikey, M0_CA_NSKEY_FREE, &cob);
	M0_UT_ASSERT(rc == 0);

	m0_sm_group_lock(grp);
	m0_dtx_init(&tx, dbenv->d_i.d_seg->bs_domain, grp);
	m0_cob_tx_credit(cob->co_dom, M0_COB_OP_DELETE_PUT, &tx.tx_betx_cred);
	rc = m0_dtx_open_sync(&tx);
	M0_ASSERT(rc == 0);
	rc = m0_cob_delete_put(cob, &tx.tx_betx);
	M0_UT_ASSERT(rc == 0);
	m0_dtx_done_sync(&tx);
	m0_dtx_fini(&tx);
	m0_sm_group_unlock(grp);
}

static void buf_put(struct m0_sns_cm_cp *scp)
{
	m0_cm_cp_buf_release(&scp->sc_base);
}

static void repair_ag_destroy(const struct m0_tl_descr *descr, struct m0_tl *head)
{
	struct m0_sns_cm_repair_ag *rag;
	struct m0_cm_aggr_group    *ag;
	struct m0_cm_cp            *cp;
	int                         i;

	m0_tlist_for(descr, head, ag) {
		rag = sag2repairag(ag2snsag(ag));
		for (i = 0; i < rag->rag_base.sag_fnr; ++i) {
			cp = &rag->rag_fc[i].fc_tgt_acc_cp.sc_base;
			buf_put(&rag->rag_fc[i].fc_tgt_acc_cp);
			m0_cm_cp_only_fini(cp);
			cp->c_ops->co_free(cp);
		}
		ag->cag_ops->cago_fini(ag);
	} m0_tlist_endfor;
}

static void ag_destroy(void)
{
	repair_ag_destroy(&aggr_grps_in_tl, &cm->cm_aggr_grps_in);
	repair_ag_destroy(&aggr_grps_out_tl, &cm->cm_aggr_grps_out);
}

static void cobs_create(uint64_t nr_files, uint64_t nr_cobs)
{
	struct m0_cob_domain *cdom;
	struct m0_dbenv      *dbenv;
	struct m0_fid         gfid;
	uint32_t              cob_idx;
	int                   i;
	int                   j;

	dbenv_cob_domain_get(&dbenv, &cdom);
	gfid.f_container = 0;
	for (i = 0; i < nr_files; ++i) {
		gfid.f_key = ITER_GOB_KEY_START + i;
		cob_idx = 0;
		for (j = 1; j <= nr_cobs; ++j) {
			cob_create(dbenv, cdom, j, &gfid, cob_idx);
			cob_idx++;
		}
	}
}

static void cobs_delete(uint64_t nr_files, uint64_t nr_cobs)
{
	int i;
	int j;

	for (i = 0; i < nr_files; ++i) {
		for (j = 1; j <= nr_cobs; ++j)
			cob_delete(j, ITER_GOB_KEY_START + i);
	}
}

static int iter_run(uint64_t pool_width, uint64_t nr_files)
{
	struct m0_sns_cm_cp  scp;
	struct m0_sns_cm_ag *sag;
	int                  rc;

	m0_fi_enable("m0_sns_cm_file_size_layout_fetch", "ut_layout_fsize_fetch");
	m0_fi_enable("iter_fid_next", "ut_layout_fsize_fetch");
	m0_fi_enable("m0_sns_cm_fctx_ag_incr", "do_nothing");

	cobs_create(nr_files, pool_width);
	m0_cm_lock(cm);
	do {
		M0_SET0(&scp);
		scp.sc_base.c_ops = &m0_sns_cm_repair_cp_ops;
		m0_cm_cp_only_init(cm, &scp.sc_base);
		scm->sc_it.si_cp = &scp;
		rc = m0_sns_cm_iter_next(cm, &scp.sc_base);
		if (rc == M0_FSO_AGAIN) {
			M0_UT_ASSERT(cp_verify(&scp));
			sag = ag2snsag(scp.sc_base.c_ag);
			M0_ASSERT(sag->sag_base.cag_layout != NULL);
		}
		buf_put(&scp);
		m0_cm_cp_only_fini(&scp.sc_base);
	} while (rc == M0_FSO_AGAIN);
	m0_cm_unlock(cm);
	m0_fi_disable("m0_sns_cm_fctx_ag_incr", "do_nothing");
	m0_fi_disable("m0_sns_cm_file_size_layout_fetch", "ut_layout_fsize_fetch");
	m0_fi_disable("iter_fid_next", "ut_layout_fsize_fetch");

	return rc;
}

static void iter_stop(uint64_t pool_width, uint64_t nr_files, uint64_t fd)
{
	int rc;

	m0_cm_lock(cm);
	/* Destroy previously created aggregation groups manually. */
	m0_fi_enable("m0_sns_cm_fctx_ag_dec", "do_nothing");
	m0_fi_enable("m0_sns_cm_fid_check_unlock", "do_nothing");
	ag_destroy();
	m0_fi_disable("m0_sns_cm_fid_check_unlock", "do_nothing");
	m0_fi_disable("m0_sns_cm_fctx_ag_dec", "do_nothing");
	rc = cm->cm_ops->cmo_stop(cm);
	M0_UT_ASSERT(rc == 0);
	m0_cm_unlock(cm);
	cobs_delete(nr_files, pool_width);
	/* Transition the failed device M0_PNDS_SNS_REBALANCING->M0_PNDS_ONLINE,
	 * for subsequent failure tests so that pool machine doesn't interpret
	 * it as a multiple failure after reading from the persistence store.
	 */
	pool_mach_transit(cm->cm_pm, fd, M0_PNDS_SNS_REBALANCING);
	pool_mach_transit(cm->cm_pm, fd, M0_PNDS_ONLINE);
	cs_fini(&sctx);
	/* Cleanup the old db files. Otherwise it messes */
	//system("rm -r sr_db > /dev/null");
}

static void iter_repair_single_file(void)
{
	int       rc;

	iter_setup(SNS_REPAIR, 2);
	rc = iter_run(10, 1);
	M0_UT_ASSERT(rc == -ENODATA);
	iter_stop(10, 1, 2);
}

static void iter_repair_multi_file(void)
{
	int       rc;

	iter_setup(SNS_REPAIR, 5);
	rc = iter_run(10, 2);
	M0_UT_ASSERT(rc == -ENODATA);
	iter_stop(10, 2, 5);
}

static void iter_repair_large_file_with_large_unit_size(void)
{
	int       rc;

	iter_setup(SNS_REPAIR, 9);
	rc = iter_run(10, 1);
	M0_UT_ASSERT(rc == -ENODATA);
	iter_stop(10, 1, 9);
}

/*
static void iter_rebalance_single_file(void)
{
	int       rc;

	iter_setup(SNS_REBALANCE, 2);
	rc = iter_run(10, 1);
	M0_UT_ASSERT(rc == -ENODATA);
	iter_stop(1, 5);
}

static void iter_rebalance_multi_file(void)
{
	int       rc;

	iter_setup(SNS_REBALANCE, 5);
	rc = iter_run(10, 2);
	M0_UT_ASSERT(rc == -ENODATA);
	iter_stop(2, 10);
}

static void iter_rebalance_large_file_with_large_unit_size(void)
{
	int       rc;

	iter_setup(SNS_REBALANCE, 9);
	rc = iter_run(10, 1);
	M0_UT_ASSERT(rc == -ENODATA);
	iter_stop(1, 10);
}
*/

static void iter_invalid_nr_cobs(void)
{
	int rc;

	iter_setup(SNS_REPAIR, 7);
	rc = iter_run(3, 1);
	M0_UT_ASSERT(rc == -ENODATA);
	iter_stop(3, 1, 7);
}

const struct m0_test_suite sns_cm_repair_ut = {
	.ts_name = "sns-cm-repair-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "service-startstop", service_start_success},
		{ "service-init-fail", service_init_failure},
		{ "service-start-fail", service_start_failure},
		{ "iter-repair-single-file", iter_repair_single_file},
		{ "iter-repair-multi-file", iter_repair_multi_file},
		{ "iter-repair-large-file-with-large-unit-size",
		  iter_repair_large_file_with_large_unit_size},
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
