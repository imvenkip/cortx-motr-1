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
 * Original author: Andriy Tkachuk <andriy_tkachuk@xyratex.com>
 * Original creation date: 04/01/2013
 */

#include "lib/memory.h"
#include "lib/misc.h"

#include "fop/fop_addb.h"	/* m0_addb_ct_fop_mod */

#include "fop/ut/stats/stats_fom.h"


static struct m0_chan   chan;
static struct m0_mutex  mutex;
static struct m0_clink	clink;

static struct m0_sm_state_descr phases[] = {
	[PH_INIT] = {
		.sd_flags     = M0_SDF_INITIAL,
		.sd_name      = "Init",
		.sd_allowed   = M0_BITS(PH_RUN, PH_FINISH)
	},
	[PH_RUN] = {
		.sd_name      = "fom_run",
		.sd_allowed   = M0_BITS(PH_FINISH)
	},
	[PH_FINISH] = {
		.sd_flags     = M0_SDF_TERMINAL,
		.sd_name      = "SM finish",
	}
};

static struct m0_sm_trans_descr trans[TRANS_NR] = {
	{ "Start",    PH_INIT,   PH_RUN },
	{ "Failed",   PH_INIT,   PH_FINISH },
	{ "Stop",     PH_RUN,    PH_FINISH }
};

static struct m0_sm_conf fom_phases_conf = {
	.scf_name      = "FOM phases",
	.scf_nr_states = ARRAY_SIZE(phases),
	.scf_state     = phases,
	.scf_trans_nr  = ARRAY_SIZE(trans),
	.scf_trans     = trans
};

#ifdef FOM_PHASE_STATS_HIST_ARGS
M0_ADDB_RT_SM_CNTR(addb_rt_fom_phase_stats, ADDB_RECID_FOM_PHASE_STATS,
		   &fom_phases_conf, FOM_STATE_STATS_HIST_ARGS);
#else
M0_ADDB_RT_SM_CNTR(addb_rt_fom_phase_stats, ADDB_RECID_FOM_PHASE_STATS,
		   &fom_phases_conf);
#endif

static size_t fom_stats_home_locality(const struct m0_fom *fom);
static void fop_stats_fom_fini(struct m0_fom *fom);
static int fom_stats_tick(struct m0_fom *fom);
static void fom_stats_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc);

extern struct m0_reqh_service_type ut_stats_service_type;

/** Ops object for fom_stats FOM */
static const struct m0_fom_ops fom_stats_ops = {
	.fo_fini           = fop_stats_fom_fini,
	.fo_tick           = fom_stats_tick,
	.fo_home_locality  = fom_stats_home_locality,
	.fo_addb_init      = fom_stats_addb_init
};

/** FOM type specific functions for stats FOP. */
const struct m0_fom_type_ops fom_stats_type_ops = {
	.fto_create = NULL
};

/** Stats specific FOM type operations vector. */
struct m0_fom_type stats_fom_type;

static size_t fom_stats_home_locality(const struct m0_fom *fom)
{
	static size_t locality = 0;

	M0_PRE(fom != NULL);
	return locality++;
}

static void fom_stats_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc)
{
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &fom->fo_addb_ctx, &m0_addb_ct_fop_mod,
			 &m0_addb_proc_ctx);
}

static int stats_fom_create(struct m0_fom **m, struct m0_reqh *reqh)
{
        struct m0_fom     *fom;
        struct fom_stats  *fom_obj;

        M0_PRE(m != NULL);

        M0_ALLOC_PTR(fom_obj);
        M0_UT_ASSERT(fom_obj != NULL);

	fom = &fom_obj->fs_gen;
	m0_fom_init(fom, &stats_fom_type, &fom_stats_ops, NULL, NULL,
	            reqh, &ut_stats_service_type);

	m0_addb_sm_counter_init(&fom_obj->fs_phase_stats,
				&addb_rt_fom_phase_stats,
				fom_obj->fs_stats_data,
				sizeof(fom_obj->fs_stats_data));
	m0_fom_phase_stats_enable(fom, &fom_obj->fs_phase_stats);

	*m = fom;
	return 0;
}

static void fop_stats_fom_fini(struct m0_fom *fom)
{
	struct fom_stats *fom_obj;
	struct m0_sm *sm;

	fom_obj = container_of(fom, struct fom_stats, fs_gen);

	sm = &fom->fo_sm_phase;

	M0_UT_ASSERT(sm->sm_addb_stats != NULL);
	M0_UT_ASSERT(sm->sm_addb_stats == &fom_obj->fs_phase_stats);

	M0_UT_ASSERT(sm->sm_addb_stats->asc_data != NULL);
	/* init -> run */
	M0_UT_ASSERT(sm->sm_addb_stats->asc_data[0].acd_nr == 1);
	M0_UT_ASSERT(sm->sm_addb_stats->asc_data[0].acd_total >= 10);
	/* init -> fail: we don't use this transition */
	M0_UT_ASSERT(sm->sm_addb_stats->asc_data[1].acd_nr == 0);
	M0_UT_ASSERT(sm->sm_addb_stats->asc_data[1].acd_total == 0);
	/* run -> fini */
	M0_UT_ASSERT(sm->sm_addb_stats->asc_data[2].acd_nr == 1);
	M0_UT_ASSERT(sm->sm_addb_stats->asc_data[2].acd_total >= 10);

	m0_fom_fini(fom);
	m0_addb_sm_counter_fini(&fom_obj->fs_phase_stats);
	m0_free(fom_obj);

	m0_chan_signal_lock(&chan);

	return;
}

static int fom_stats_tick(struct m0_fom *fom)
{
	if (m0_fom_phase(fom) == PH_INIT) {
		/* Must sleep to advance the clock */
		m0_nanosleep(10000, NULL);
		m0_fom_phase_set(fom, PH_RUN);
		return M0_FSO_AGAIN;
	} else if (m0_fom_phase(fom) == PH_RUN) {
		m0_nanosleep(10000, NULL);
		m0_fom_phase_set(fom, M0_FOM_PHASE_FINISH);
		return M0_FSO_WAIT;
	} else {
		M0_UT_ASSERT(0); /* we should not get here */
		return 0;
	}
}

static void test_stats_req_handle(struct m0_reqh *reqh)
{
	struct m0_fom   *fom;
	struct fom_stats *obj;
	int rc;

	m0_mutex_init(&mutex);
	m0_chan_init(&chan, &mutex);
	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&chan, &clink);

	rc = stats_fom_create(&fom, reqh);
	M0_UT_ASSERT(rc == 0);

	obj = container_of(fom, struct fom_stats, fs_gen);
	m0_fom_queue(fom, reqh);

	m0_chan_wait(&clink);

	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);
	m0_chan_fini_lock(&chan);
	m0_mutex_fini(&mutex);
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
