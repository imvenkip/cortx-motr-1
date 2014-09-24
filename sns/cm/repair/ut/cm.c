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
#include "fop/fom_simple.h"
#include "ioservice/io_service.h"
#include "ioservice/io_device.h"
#include "pool/pool.h"
#include "mdservice/md_fid.h"
#include "rm/rm_service.h"                 /* m0_rms_type */
#include "sns/cm/repair/ag.h"
#include "sns/cm/cm.h"
#include "sns/cm/file.h"
#include "sns/cm/repair/ut/cp_common.h"

enum {
	ITER_UT_BUF_NR     = 1 << 8,
	ITER_GOB_KEY_START = 4,
};

enum {
	ITER_RUN = M0_FOM_PHASE_FINISH + 1,
	ITER_WAIT,
};

static struct m0_reqh          *reqh;
static struct m0_reqh_service  *service;
static struct m0_cm            *cm;
static struct m0_sns_cm        *scm;
static struct m0_sns_cm_cp      scp;
static struct m0_sns_cm_ag     *sag;
static struct m0_fom_simple     iter_fom;
static struct m0_semaphore      iter_sem;

static struct m0_sm_state_descr iter_ut_fom_phases[] = {
	[M0_FOM_PHASE_INIT] = {
		.sd_name      = "init",
		.sd_allowed   = M0_BITS(ITER_RUN),
		.sd_flags     = M0_SDF_INITIAL
	},
	[ITER_RUN] = {
		.sd_name      = "Iterator run",
		.sd_allowed   = M0_BITS(ITER_WAIT, M0_FOM_PHASE_INIT,
					M0_FOM_PHASE_FINISH)
	},
	[ITER_WAIT] = {
		.sd_name      = "Iterator wait",
		.sd_allowed   = M0_BITS(ITER_RUN, M0_FOM_PHASE_INIT,
					M0_FOM_PHASE_FINISH)
	},
	[M0_FOM_PHASE_FINISH] = {
		.sd_name      = "fini",
		.sd_flags     = M0_SDF_TERMINAL
	}
};

static struct m0_sm_conf iter_ut_conf = {
	.scf_name      = "iter ut fom phases",
	.scf_nr_states = ARRAY_SIZE(iter_ut_fom_phases),
	.scf_state     = iter_ut_fom_phases,
};

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

void iter_swu_wakeme(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
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
        service = m0_reqh_service_find(&m0_rms_type, reqh),
        M0_ASSERT(service != NULL);
	cm->cm_sw_update.swu_wakeme_ast.sa_cb = iter_swu_wakeme;
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
	int rc;

	*dbenv = cm->cm_service.rs_reqh->rh_dbenv;
	M0_UT_ASSERT(dbenv != NULL);
	rc = m0_ios_cdom_get(cm->cm_service.rs_reqh, cdom);
	M0_UT_ASSERT(rc == 0);
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

M0_INTERNAL void cob_delete(struct m0_dbenv *dbenv, struct m0_cob_domain *cdom,
			    uint64_t cont, uint64_t key)
{
	struct m0_sm_group   *grp = m0_locality0_get()->lo_grp;
	struct m0_cob        *cob;
	struct m0_fid         cob_fid;
	struct m0_dtx         tx;
	struct m0_cob_oikey   oikey;
	int                   rc;

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
		m0_cm_unlock(cm);
		m0_cm_lock(cm);
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
		gfid.f_key = M0_MDSERVICE_START_FID.f_key + i;
		cob_idx = 0;
		for (j = 1; j <= nr_cobs; ++j) {
			cob_create(dbenv, cdom, j, &gfid, cob_idx);
			cob_idx++;
		}
	}
}

static void cobs_delete(uint64_t nr_files, uint64_t nr_cobs)
{
	struct m0_dbenv      *dbenv;
	struct m0_cob_domain *cdom;
	int                   i;
	int                   j;

	dbenv_cob_domain_get(&dbenv, &cdom);
	for (i = 0; i < nr_files; ++i) {
		for (j = 1; j <= nr_cobs; ++j)
			cob_delete(dbenv, cdom, j, M0_MDSERVICE_START_FID.f_key + i);
	}
}

static int iter_ut_fom_tick(struct m0_fom *fom, uint32_t  *sem_id, int *phase)
{
	int rc = M0_FSO_AGAIN;

	switch (*phase) {
		case M0_FOM_PHASE_INIT:
			M0_SET0(&scp);
			scp.sc_base.c_ops = &m0_sns_cm_repair_cp_ops;
			m0_cm_cp_only_init(cm, &scp.sc_base);
			scm->sc_it.si_cp = &scp;
			*phase = ITER_RUN;
			rc = M0_FSO_AGAIN;
			break;
		case ITER_RUN:
			m0_cm_lock(cm);
			rc = m0_sns_cm_iter_next(cm, &scp.sc_base);
			if (rc == M0_FSO_AGAIN) {
				M0_UT_ASSERT(cp_verify(&scp));
				sag = ag2snsag(scp.sc_base.c_ag);
				M0_ASSERT(sag->sag_base.cag_layout != NULL);
				buf_put(&scp);
				m0_cm_cp_only_fini(&scp.sc_base);
				*phase = M0_FOM_PHASE_INIT;
			}
			if (rc == M0_FSO_WAIT || rc == -ENOBUFS) {
				*phase = ITER_WAIT;
				rc = M0_FSO_WAIT;
			}
			if (rc == -ENODATA) {
				*phase = M0_FOM_PHASE_FINISH;
				rc = M0_FSO_WAIT;
				m0_semaphore_up(&iter_sem);
			}
			m0_cm_unlock(cm);
			break;
		case ITER_WAIT:
			*phase = ITER_RUN;
			rc = M0_FSO_AGAIN;
			break;
	}

	return rc;
}

static void iter_run(uint64_t pool_width, uint64_t nr_files)
{
	m0_fi_enable("m0_sns_cm_file_size_layout_fetch", "ut_layout_fsize_fetch");
	m0_fi_enable("iter_fid_attr_fetch", "ut_attr_fetch");
	m0_fi_enable("iter_fid_attr_fetch_wait", "ut_attr_fetch_wait");
	m0_fi_enable("iter_fid_layout_fetch", "ut_layout_fsize_fetch");
	m0_fi_enable("iter_fid_next", "ut_fid_next");

	cobs_create(nr_files, pool_width);
	scm->sc_it.si_fom = &iter_fom.si_fom;
	m0_semaphore_init(&iter_sem, 0);
	M0_FOM_SIMPLE_POST(&iter_fom, reqh, &iter_ut_conf,
			   &iter_ut_fom_tick, NULL, 2);
	m0_semaphore_down(&iter_sem);
	m0_semaphore_fini(&iter_sem);

	m0_fi_disable("m0_sns_cm_file_size_layout_fetch", "ut_layout_fsize_fetch");
	m0_fi_disable("iter_fid_attr_fetch", "ut_attr_fetch");
	m0_fi_disable("iter_fid_attr_fetch_wait", "ut_attr_fetch_wait");
	m0_fi_disable("iter_fid_layout_fetch", "ut_layout_fsize_fetch");
	m0_fi_disable("iter_fid_next", "ut_fid_next");
}

static void iter_stop(uint64_t pool_width, uint64_t nr_files, uint64_t fd)
{
	int rc;

	m0_cm_lock(cm);
	ag_destroy();
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
}

static void iter_repair_single_file(void)
{
	iter_setup(SNS_REPAIR, 2);
	iter_run(10, 1);
	iter_stop(10, 1, 2);
}

static void iter_repair_multi_file(void)
{
	iter_setup(SNS_REPAIR, 5);
	iter_run(10, 2);
	iter_stop(10, 2, 5);
}

static void iter_repair_large_file_with_large_unit_size(void)
{
	iter_setup(SNS_REPAIR, 9);
	iter_run(10, 1);
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
	iter_setup(SNS_REPAIR, 7);
	iter_run(3, 1);
	iter_stop(3, 1, 7);
}

struct m0_ut_suite sns_cm_repair_ut = {
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
