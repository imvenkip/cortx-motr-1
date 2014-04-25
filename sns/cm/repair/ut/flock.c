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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 12/12/2013
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/stat.h>
#include "lib/misc.h"
#include "lib/memory.h"
#include "lib/finject.h"
#include "lib/locality.h"

#include "reqh/reqh_service.h"
#include "reqh/reqh.h"
#include "mero/setup.h"

#include "sns/cm/repair/ag.h"
#include "sns/cm/cm.h"
#include "sns/cm/repair/ut/cp_common.h"
#include "fop/fom_simple.h"
#include "mdservice/md_fid.h"
#include "rm/rm_service.h"                 /* m0_rms_type */

enum {
	SINGLE_THREAD_TEST = 1,
	MULTI_THREAD_TEST,
	KEY_START = 1ULL << 16,
	NR = 3,
	KEY_MAX = KEY_START + NR,
	NR_FIDS = 20,
};

static struct m0_reqh		*reqh;
static struct m0_reqh_service   *service;
static struct m0_cm		*cm;
static struct m0_sns_cm		*scm;
static struct m0_fom_simple	fs[NR];
static struct m0_fid		test_fids[NR_FIDS];
static struct m0_semaphore	sem[NR_FIDS];
static struct m0_fid		gfid;
static int			fid_index;

enum {
	FILE_LOCK = M0_FOM_PHASE_FINISH + 1,
	FILE_LOCK_WAIT,
};

static struct m0_sm_state_descr flock_ut_fom_phases[] = {
	[M0_FOM_PHASE_INIT] = {
		.sd_name      = "init",
		.sd_allowed   = M0_BITS(FILE_LOCK),
		.sd_flags     = M0_SDF_INITIAL
	},
	[FILE_LOCK] = {
		.sd_name      = "File lock acquire",
		.sd_allowed   = M0_BITS(FILE_LOCK_WAIT, M0_FOM_PHASE_FINISH)
	},
	[FILE_LOCK_WAIT] = {
		.sd_name      = "Wait for file lock",
		.sd_allowed   = M0_BITS(FILE_LOCK_WAIT, M0_FOM_PHASE_FINISH)
	},
	[M0_FOM_PHASE_FINISH] = {
		.sd_name      = "fini",
		.sd_flags     = M0_SDF_TERMINAL
	}
};

static struct m0_sm_conf flock_ut_conf = {
	.scf_name      = "flock ut fom phases",
	.scf_nr_states = ARRAY_SIZE(flock_ut_fom_phases),
	.scf_state     = flock_ut_fom_phases,
};

static int flock_ut_fom_tick(struct m0_fom *fom, uint32_t  *sem_id, int *phase)
{
	int		           rc;
	struct m0_chan	          *rm_chan;
	struct m0_sns_cm_file_ctx *fctx;
	struct m0_fid		  *fid;

	fid = &gfid;

	m0_mutex_lock(&scm->sc_file_ctx_mutex);
	switch (*phase) {
	case M0_FOM_PHASE_INIT:
		*phase = FILE_LOCK;
		rc = M0_FSO_AGAIN;
		goto end;

	case FILE_LOCK:
		fctx = m0_scmfctx_htable_lookup(&scm->sc_file_ctx, fid);
		if (fctx != NULL) {
			if (fctx->sf_flock_status == M0_SCM_FILE_LOCKED) {
			m0_ref_get(&fctx->sf_ref);
			rc = -1;
			goto end;
			}
			if (fctx->sf_flock_status == M0_SCM_FILE_LOCK_WAIT) {
				rc = M0_FSO_AGAIN;
				*phase = FILE_LOCK_WAIT;
				goto end;
			}
		}
		rc = m0_sns_cm_fctx_init(scm, fid, &fctx);
		if (rc != 0) {
			rc = M0_FSO_AGAIN;
			*phase = M0_FOM_PHASE_FINISH;
			goto end;
		}
		rc = m0_sns_cm_file_lock(fctx);
		M0_UT_ASSERT(rc == M0_FSO_WAIT);
		rm_chan = &fctx->sf_rin.rin_sm.sm_chan;
		m0_rm_owner_lock(&fctx->sf_owner);
		m0_fom_wait_on(fom, rm_chan, &fom->fo_cb);
		m0_rm_owner_unlock(&fctx->sf_owner);
		*phase = FILE_LOCK_WAIT;
		goto end;

	case FILE_LOCK_WAIT:
		fctx = m0_scmfctx_htable_lookup(&scm->sc_file_ctx, fid);
		M0_UT_ASSERT(fctx != NULL);
		rc = m0_sns_cm_file_lock_wait(fctx, fom);
		M0_UT_ASSERT(rc == 0 || rc == M0_FSO_WAIT || rc == -EAGAIN);
		if (rc == M0_FSO_WAIT)
			goto end;
		if (rc == 0 || rc == -EAGAIN) {
			m0_ref_get(&fctx->sf_ref);
			M0_UT_ASSERT(fctx->sf_flock_status ==
				     M0_SCM_FILE_LOCKED);
			rc = -1;
			goto end;
		}

	default:
		rc = -1;
	}

end:
	if (fctx->sf_flock_status == M0_SCM_FILE_LOCKED && rc == -1)
		m0_semaphore_up(&sem[*sem_id]);
	m0_mutex_unlock(&scm->sc_file_ctx_mutex);
	return rc;
}

static int file_lock_verify(struct m0_sns_cm *scm, struct m0_fid *fid,
			    int64_t ref)
{
	struct m0_sns_cm_file_ctx *fctx;

	fctx = m0_sns_cm_fctx_locate(scm, fid);
	M0_UT_ASSERT(fctx != NULL);
	M0_UT_ASSERT(fctx->sf_flock_status == M0_SCM_FILE_LOCKED);
	M0_UT_ASSERT(m0_fid_eq(&fctx->sf_fid, fid));
	M0_UT_ASSERT(m0_ref_read(&fctx->sf_ref) == ref);
	return 0;
}

static void sns_flock_multi_fom(void)
{
	uint32_t		   i;
	struct m0_sns_cm_file_ctx *fctx;

	M0_SET0(&fs);
	m0_fid_set(&gfid, 0, KEY_START);
	for (i = 0; i < NR; i++) {
		m0_semaphore_init(&sem[i], 0);
		M0_FOM_SIMPLE_POST(&fs[i], reqh, &flock_ut_conf,
				   &flock_ut_fom_tick, &i, 2);
		m0_semaphore_down(&sem[i]);
		m0_semaphore_fini(&sem[i]);
	}
	m0_mutex_lock(&scm->sc_file_ctx_mutex);
	file_lock_verify(scm, &gfid, NR);
	for (i = 0; i < NR; ++i) {
		fctx = m0_sns_cm_fctx_locate(scm, &gfid);
		M0_UT_ASSERT(fctx != NULL);
		m0_sns_cm_file_unlock(fctx);
	}
	m0_mutex_unlock(&scm->sc_file_ctx_mutex);
}

static void sns_flock_single_fom(void)
{
	uint32_t		   sem_id = 0;
	struct m0_sns_cm_file_ctx *fctx;

	m0_fid_set(&gfid, 0, KEY_START);

	m0_semaphore_init(&sem[sem_id], 0);
	M0_FOM_SIMPLE_POST(&fs[0], reqh, &flock_ut_conf,
			   &flock_ut_fom_tick, &sem_id, 2);
	m0_semaphore_down(&sem[0]);
	m0_semaphore_fini(&sem[sem_id]);
	m0_mutex_lock(&scm->sc_file_ctx_mutex);
	fctx = m0_sns_cm_fctx_locate(scm, &test_fids[0]);
	m0_sns_cm_file_unlock(fctx);
	m0_mutex_unlock(&scm->sc_file_ctx_mutex);
}

static bool flock_cb(struct m0_clink *clink)
{
	struct m0_sns_cm_file_ctx *fctx;
	uint32_t		   state;

	fctx = m0_scmfctx_htable_lookup(&scm->sc_file_ctx,
					&test_fids[fid_index]);
	M0_UT_ASSERT(fctx != NULL);
	state = fctx->sf_rin.rin_sm.sm_state;
	if (state == RI_SUCCESS) {
		fctx->sf_flock_status = M0_SCM_FILE_LOCKED;
		return false;
	}
		return true;
}

static int test_setup(void)
{
	int                  rc;

        rc = cs_init(&sctx);
        M0_ASSERT(rc == 0);
	reqh = m0_cs_reqh_get(&sctx);
	M0_ASSERT(reqh != NULL);
	service = m0_reqh_service_find(
		  m0_reqh_service_type_find("sns_repair"),reqh);
	M0_ASSERT(service != NULL);
	cm = container_of(service, struct m0_cm, cm_service);
	M0_ASSERT(cm != NULL);
	scm = cm2sns(cm);
	M0_ASSERT(scm != NULL);
	rc = m0_sns_cm_rm_init(scm);
	M0_ASSERT(rc == 0);
	service = m0_reqh_service_find(&m0_rms_type, reqh),
	M0_ASSERT(service != NULL);
	return 0;
}

static int test_fini(void)
{
	m0_sns_cm_rm_fini(scm);
	cs_fini(&sctx);
	return 0;
}

static int fids_set(void)
{
	uint64_t cont = 0;
	uint64_t key = KEY_START;
	int	 i;

	for (i = 0; i < NR_FIDS; ++i) {
		m0_fid_set(&test_fids[i], cont, key);
		key++;
		if (key == KEY_MAX) {
		cont++;
		key = KEY_START;
		}
	}
	return 0;
}

static void sns_file_lock_unlock(void)
{
	int			   rc;
	struct m0_sns_cm_file_ctx *fctx[NR_FIDS];
	struct m0_chan		  *chan;
	int			   i;
	struct m0_clink		   tc_clink[NR_FIDS];
	uint64_t		   cont = 0;
	uint64_t		   key = KEY_START;
	struct m0_fid		   fid;

	fids_set();
	m0_mutex_lock(&scm->sc_file_ctx_mutex);
	for (i = 0; i < NR_FIDS; i++) {
		fid_index = i;
		m0_clink_init(&tc_clink[i], &flock_cb);
		rc = m0_sns_cm_fctx_init(scm, &test_fids[i], &fctx[i]);
		M0_UT_ASSERT(rc == 0);
		chan = &fctx[i]->sf_rin.rin_sm.sm_chan;
		m0_rm_owner_lock(&fctx[i]->sf_owner);
		m0_clink_add(chan, &tc_clink[i]);
		m0_rm_owner_unlock(&fctx[i]->sf_owner);
		rc = m0_sns_cm_file_lock(fctx[i]);
		M0_UT_ASSERT(rc == M0_FSO_WAIT);
		m0_ref_get(&fctx[i]->sf_ref);
		m0_chan_wait(&tc_clink[i]);
		m0_fid_set(&fid, cont, key);
		file_lock_verify(scm, &fid, 1);
		++key;
		if (key == KEY_MAX) {
			cont++;
			key = KEY_START;
		}
		m0_rm_owner_lock(&fctx[i]->sf_owner);
		m0_clink_del(&tc_clink[i]);
		m0_clink_fini(&tc_clink[i]);
		m0_rm_owner_unlock(&fctx[i]->sf_owner);
		m0_sns_cm_file_unlock(fctx[i]);
	}
	m0_mutex_unlock(&scm->sc_file_ctx_mutex);
}

const struct m0_test_suite sns_flock_ut = {
	.ts_name = "sns-file-lock-ut",
	.ts_init = test_setup,
	.ts_fini = test_fini,
	.ts_tests = {
		{ "sns-file-lock-unlock", sns_file_lock_unlock},
		{ "sns-flock-single-fom", sns_flock_single_fom},
		{ "sns-flock-multi-fom", sns_flock_multi_fom},
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
