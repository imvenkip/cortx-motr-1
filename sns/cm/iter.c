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
 * Original creation date: 10/08/2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/trace.h"

#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "lib/finject.h"

#include "cob/cob.h"
#include "mdstore/mdstore.h"
#include "reqh/reqh.h"
#include "ioservice/io_service.h"

#include "cm/proxy.h"
#include "sns/sns_addb.h"
#include "sns/parity_repair.h"
#include "sns/cm/cm.h"
#include "sns/cm/cp.h"
#include "sns/cm/ag.h"
#include "sns/cm/cm_utils.h"

/**
  @addtogroup SNSCM

  @{
*/

enum {
        SNS_REPAIR_ITER_MAGIX = 0x33BAADF00DCAFE77,
};

static const struct m0_bob_type iter_bob = {
	.bt_name = "sns cm data iterator",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_sns_cm_iter, si_magix),
	.bt_magix = SNS_REPAIR_ITER_MAGIX,
	.bt_check = NULL
};

M0_BOB_DEFINE(static, &iter_bob, m0_sns_cm_iter);

enum cm_data_iter_phase {
	/**
	 * Iterator is in this phase when m0_cm:cm_ops::cmo_data_next() is first
	 * invoked as part of m0_cm_start() and m0_cm_cp_pump_start(), from
	 * m0_cm_data_next(). This starts the sns repair data iterator and sets
	 * the iterator to first local data unit of a parity group from a GOB
	 * (file) that needs repair.
	 */
	ITPH_IDLE,
	/**
	 * Iterator is in this phase until all the local data units of a
	 * parity group are serviced (i.e. copy packets are created).
	 */
	ITPH_COB_NEXT,
	/**
	 * Iterator transitions to this phase to select next parity group
	 * that needs to be repaired, and has local data units.
	 */
	ITPH_GROUP_NEXT,
	/**
	 * Iterator transitions to this phase when it blocks on certain
	 * operation in ITPH_GROUP_NEXT phase (i.e. fetching file size) and
	 * waits for the completion event.
	 */
	ITPH_GROUP_NEXT_WAIT,
	/**
	 * Iterator transitions to this phase in-order to select next GOB that
	 * needs repair.
	 */
	ITPH_FID_NEXT,
	/** Iterator tries to acquire the async rm file lock in this phase. */
	ITPH_FID_LOCK,
	/** Iterator waits for the rm file lock to be acquired in this phase. */
	ITPH_FID_LOCK_WAIT,
	/** Iterator fetches GOB attributes in this phase. */
	ITPH_FID_ATTR_FETCH,
	/** Iterator waits till GOB attributes are fetched asynchronously. */
	ITPH_FID_ATTR_FETCH_WAIT,
	/** Iterator fetches GOB layout in this phase. */
	ITPH_FID_LAYOUT_FETCH,
	/** Iterator waits till GOB layout is fetched asynchronously. */
	ITPH_FID_LAYOUT_FETCH_WAIT,
	/**
	 * Once next local data unit of parity group needing repair is calculated
	 * along with its corresponding COB fid, the pre allocated copy packet
	 * by the copy machine pump FOM is populated with required details.
	 * @see struct m0_sns_cm_cp
	 */
	ITPH_CP_SETUP,
	/**
	 * Once the aggregation group is created and initialised, we need to
	 * acquire buffers for accumulator copy packet in the aggregation group
	 * falure contexts. This operation may block.
	 * @see m0_sns_cm_ag::sag_fc
	 * @see struct m0_sns_cm_ag_failure_ctx
	 */
	ITPH_AG_SETUP,
	/**
	 * Iterator is finalised after the sns repair operation is complete.
	 * This is done as part of m0_cm_stop().
	 */
	ITPH_FINI,
	ITPH_NR
};

M0_INTERNAL struct m0_sns_cm *it2sns(struct m0_sns_cm_iter *it)
{
	return container_of(it, struct m0_sns_cm, sc_it);
}

/**
 * Returns current iterator phase.
 */
static enum cm_data_iter_phase iter_phase(const struct m0_sns_cm_iter *it)
{
	return it->si_sm.sm_state;
}

/**
 * Sets iterator phase.
 */
static void iter_phase_set(struct m0_sns_cm_iter *it, int phase)
{
	m0_sm_state_set(&it->si_sm, phase);
}

static bool
iter_layout_invariant(enum cm_data_iter_phase phase,
                      const struct m0_sns_cm_file_context *sfc)
{
	return ergo(M0_IN(phase, (ITPH_COB_NEXT, ITPH_GROUP_NEXT,
				 ITPH_GROUP_NEXT_WAIT, ITPH_CP_SETUP)),
		   sfc->sfc_pdlayout != NULL &&
		   sfc->sfc_pi != NULL && sfc->sfc_upg != 0 &&
		   sfc->sfc_dpupg != 0 && m0_fid_is_set(&sfc->sfc_gob_fid)) &&
	       ergo(M0_IN(phase, (ITPH_CP_SETUP)), sfc->sfc_groups_nr != 0 &&
		   m0_fid_is_set(&sfc->sfc_cob_fid) &&
		   sfc->sfc_sa.sa_group <= sfc->sfc_groups_nr &&
		   sfc->sfc_sa.sa_unit <= sfc->sfc_upg &&
		   sfc->sfc_ta.ta_obj <= m0_pdclust_P(sfc->sfc_pdlayout));
}

static bool iter_invariant(const struct m0_sns_cm_iter *it)
{
	enum cm_data_iter_phase phase = iter_phase(it);

	return it != NULL && m0_sns_cm_iter_bob_check(it) &&
	       it->si_cp != NULL && iter_layout_invariant(phase, &it->si_fc);
}

/**
 * Fetches file size and layout for it->si_fc.sfc_gob_fid.
 * @note This may block.
 * @retval 0 on success, IT_WAIT for blocking operation
 */
static int file_size_and_layout_fetch(struct m0_sns_cm_iter *it)
{
        struct m0_sns_cm         *scm = it2sns(it);
        struct m0_fid            *gfid;
        struct m0_pdclust_layout *pl = NULL;
        int                       rc;
        M0_ENTRY("it = %p", it);

        M0_PRE(it != NULL);

        gfid = &it->si_fc.sfc_gob_fid;
        rc = m0_sns_cm_file_size_layout_fetch(&scm->sc_base, gfid,
                                              &it->si_fc.sfc_pdlayout,
                                              &it->si_fc.sfc_fsize);
        if (rc == 0) {
                pl = it->si_fc.sfc_pdlayout;
                /*
                 * We need only the number of parity units equivalent
                 * to the number of failures.
                 */
                it->si_fc.sfc_dpupg = m0_pdclust_N(pl) + m0_pdclust_K(pl);
                it->si_fc.sfc_upg = m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
                it->si_total_fsize += it->si_fc.sfc_fsize;
                M0_CNT_INC(it->si_total_files);
        }

        return M0_RC(rc);
}

/**
 * Calculates COB fid for m0_sns_cm_file_context::sfc_sa.
 * Saves calculated struct m0_pdclust_tgt_addr in
 * m0_sns_cm_file_context::sfc_ta.
 */
static void unit_to_cobfid(struct m0_sns_cm_file_context *sfc,
			   struct m0_fid *cob_fid_out)
{
	struct m0_sns_cm_iter       *it;
	struct m0_sns_cm            *scm;
	struct m0_pdclust_instance  *pi;
	struct m0_pdclust_layout    *pl;
	struct m0_pdclust_src_addr  *sa;
	struct m0_pdclust_tgt_addr  *ta;
	struct m0_fid               *fid;

	it = container_of(sfc, struct m0_sns_cm_iter, si_fc);
	scm = it2sns(it);
	fid = &sfc->sfc_gob_fid;
	pi = sfc->sfc_pi;
	pl = sfc->sfc_pdlayout;
	sa = &sfc->sfc_sa;
	ta = &sfc->sfc_ta;
	sfc->sfc_cob_is_spare_unit = m0_sns_cm_unit_is_spare(scm, pl, fid,
							     sa->sa_group,
							     sa->sa_unit);
	m0_sns_cm_unit2cobfid(pl, pi, sa, ta, fid, cob_fid_out);
}

/* Uses name space iterator. */
M0_INTERNAL int __fid_next(struct m0_sns_cm_iter *it, struct m0_fid *fid_next)
{
	int             rc;

	M0_ENTRY("it = %p", it);

	rc = m0_cob_ns_iter_next(&it->si_cns_it, fid_next);

	return M0_RC(rc);
}

static void get_attr_callback(void *arg, int rc)
{
	struct m0_sns_cm      *scm;
	struct m0_cm          *cm;
	struct m0_sns_cm_iter *it = arg;

	M0_PRE(it != NULL);

	it->si_fc.sfc_fsize = it->si_fc.sfc_cob_attr.ca_size;
	scm = it2sns(it);
        cm = &scm->sc_base;
	m0_cm_cp_pump_wakeup(cm);
}

static int iter_fid_attr_fetch_wait(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm   *scm;
	struct m0_cm       *cm;

	M0_PRE(it != NULL);
	M0_ENTRY("it = %p", it);

	scm = it2sns(it);
	cm = &scm->sc_base;

	if (it->si_fc.sfc_cob_attr.ca_size == 0) {
		m0_cm_cp_pump_wait(cm);
		return M0_RC(M0_FSO_WAIT);
	}

	iter_phase_set(it, ITPH_FID_LAYOUT_FETCH);
	return M0_RC(0);
}

/** Fetches the attributes of GOB. */
static int iter_fid_attr_fetch(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm   *scm;
	struct m0_cm       *cm;
        struct m0_reqh     *reqh;
        int                 rc;

	M0_ENTRY("it = %p", it);
	M0_PRE(it != NULL);

	scm = it2sns(it);
	cm = &scm->sc_base;
        M0_PRE(m0_cm_is_locked(cm));

	reqh = cm->cm_service.rs_reqh;
	M0_SET0(&it->si_fc.sfc_cob_attr);
        rc = m0_ios_mds_getattr_async(reqh, &it->si_fc.sfc_gob_fid,
				      &it->si_fc.sfc_cob_attr,
				      &get_attr_callback, it);
	if (rc < 0 && M0_FI_ENABLED("layout_fetch_error_as_done"))
		return M0_RC(-ENODATA);

	iter_phase_set(it, ITPH_FID_ATTR_FETCH_WAIT);
	if (rc != 0)
		return M0_RC(rc);
	m0_cm_cp_pump_wait(cm);
	return M0_RC(M0_FSO_WAIT);
}

static void get_layout_callback(void *arg, int rc)
{
	struct m0_sns_cm      *scm;
	struct m0_cm          *cm;
	struct m0_sns_cm_iter *it = arg;

	M0_PRE(it != NULL);

	it->si_fc.sfc_pdlayout = m0_layout_to_pdl(it->si_fc.sfc_layout);
	scm = it2sns(it);
        cm = &scm->sc_base;
	m0_cm_cp_pump_wakeup(cm);
}

static int iter_fid_layout_fetch_wait(struct m0_sns_cm_iter *it)
{
	struct m0_pdclust_layout *pl;
	int                       rc;

	M0_PRE(it != NULL);

	pl = it->si_fc.sfc_pdlayout;

	if (pl == NULL)
		return M0_RC(M0_FSO_WAIT);

	/*
	 * We need only the number of parity units equivalent
	 * to the number of failures.
	 */
	it->si_fc.sfc_dpupg = m0_pdclust_N(pl) + m0_pdclust_K(pl);
	it->si_fc.sfc_upg = m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
	it->si_total_fsize += it->si_fc.sfc_fsize;
	M0_CNT_INC(it->si_total_files);

        rc = m0_sns_cm_fid_layout_instance(pl, &it->si_fc.sfc_pi,
					   &it->si_fc.sfc_gob_fid);
        if (rc == 0) {
                it->si_fc.sfc_sa.sa_group = 0;
                it->si_fc.sfc_sa.sa_unit = 0;
                iter_phase_set(it, ITPH_GROUP_NEXT);
        }
	return M0_RC(rc);
}

/** Fetches the layout for GOB. */
static int iter_fid_layout_fetch(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm         *scm;
	struct m0_cm             *cm;
        struct m0_reqh           *reqh;
        struct m0_layout_domain  *ldom;
	struct m0_layout         *l;
	uint64_t                  lid;
        int                       rc;

	scm = it2sns(it);
	cm = &scm->sc_base;
	reqh = cm->cm_service.rs_reqh;
	ldom = &reqh->rh_ldom;
	lid = it->si_fc.sfc_cob_attr.ca_lid;

	it->si_fc.sfc_pdlayout = NULL;
	it->si_fc.sfc_layout = NULL;

	l = m0_layout_find(ldom, lid);
	if (l != NULL) {
		it->si_fc.sfc_layout = l;
		it->si_fc.sfc_pdlayout = m0_layout_to_pdl(l);
		iter_phase_set(it, ITPH_FID_LAYOUT_FETCH_WAIT);
		return M0_RC(0);
	}

        rc = m0_ios_mds_layout_get_async(reqh, ldom, lid, &it->si_fc.sfc_layout,
                                         &get_layout_callback, it);

	if (rc < 0 && M0_FI_ENABLED("layout_fetch_error_as_done"))
		return M0_RC(-ENODATA);

	iter_phase_set(it, ITPH_FID_LAYOUT_FETCH_WAIT);
	if (rc != 0)
		return M0_RC(rc);
	m0_cm_cp_pump_wait(cm);
	return M0_RC(M0_FSO_WAIT);
}

static int iter_fid_lock(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm          *scm;
	int		           rc=0;
	struct m0_fid             *fid;
	struct m0_fom             *fom;
	struct m0_chan	          *rm_chan;
	struct m0_sns_cm_file_ctx *fctx;

	M0_ENTRY();
	M0_PRE(it != NULL);

	scm = it2sns(it);
	fid = &it->si_fc.sfc_gob_fid;
	fom = &scm->sc_base.cm_cp_pump.p_fom;
	m0_mutex_lock(&scm->sc_file_ctx_mutex);
	fctx = m0_scmfctx_htable_lookup(&scm->sc_file_ctx, fid);
	if (fctx != NULL) {
		if (fctx->sf_flock_status == M0_SCM_FILE_LOCKED) {
			rc = 0;
			M0_LOG(M0_DEBUG, "fid: <%lx, %lx>", FID_P(fid));
			m0_ref_get(&fctx->sf_ref);
			iter_phase_set(it, ITPH_FID_ATTR_FETCH);
			goto end;
		}
		if (fctx->sf_flock_status == M0_SCM_FILE_LOCK_WAIT){
			iter_phase_set(it, ITPH_FID_LOCK_WAIT);
			goto end;
		}
	}
	rc = m0_sns_cm_fctx_init(scm, fid, &fctx);
	if (rc != 0)
		goto end;

	rc = m0_sns_cm_file_lock(fctx);
	if (rc != M0_FSO_WAIT)
		goto end;

	rm_chan = &fctx->sf_rin.rin_sm.sm_chan;
	m0_rm_owner_lock(&fctx->sf_owner);
	m0_fom_wait_on(fom, rm_chan, &fom->fo_cb);
	m0_rm_owner_unlock(&fctx->sf_owner);
	iter_phase_set(it, ITPH_FID_LOCK_WAIT);
		goto end;

end:
	m0_mutex_unlock(&scm->sc_file_ctx_mutex);
	return M0_RC(rc);
}

static int iter_fid_lock_wait(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm          *scm;
	int		           rc;
	struct m0_fid             *fid;
	struct m0_fom             *fom;
	struct m0_sns_cm_file_ctx *fctx;

	M0_ENTRY();
	M0_PRE(it != NULL);

	scm = it2sns(it);
	fid = &it->si_fc.sfc_gob_fid;
	fom = &scm->sc_base.cm_cp_pump.p_fom;
	m0_mutex_lock(&scm->sc_file_ctx_mutex);
	fctx = m0_sns_cm_fctx_locate(scm, fid);
	M0_ASSERT(fctx != NULL);

	rc = m0_sns_cm_file_lock_wait(fctx, fom);
	if (rc == M0_FSO_WAIT)
		goto end;
	if (rc == 0 || rc == -EAGAIN) {
		rc = 0;
		m0_ref_get(&fctx->sf_ref);
		iter_phase_set(it, ITPH_FID_ATTR_FETCH);
		M0_ASSERT(fctx->sf_flock_status == M0_SCM_FILE_LOCKED);
	}

end:
	m0_mutex_unlock(&scm->sc_file_ctx_mutex);
	return M0_RC(rc);
}

/** Fetches next GOB fid. */
static int iter_fid_next(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm_file_context   *sfc = &it->si_fc;
	struct m0_pdclust_layout        *pl  = sfc->sfc_pdlayout;
	struct m0_fid                   *fid = &sfc->sfc_gob_fid;
	struct m0_fid                    fid_next = {0, 0};
	int                              rc;
	M0_ENTRY("it = %p", it);

	/* Get current GOB fid saved in the iterator. */
	do {
		rc = __fid_next(it, &fid_next);
	} while (rc == 0 && (m0_fid_eq(&fid_next, &M0_COB_ROOT_FID)     ||
			     m0_fid_eq(&fid_next, &M0_MDSERVICE_SLASH_FID)));

	if (rc == -ENOENT)
		return M0_RC(-ENODATA);
	if (rc == 0) {
		/* Save next GOB fid in the iterator. */
		*fid = fid_next;
		/* fini old layout instance and put old layout */
		if (sfc->sfc_pi != NULL) {
			m0_layout_instance_fini(&sfc->sfc_pi->pi_base);
			sfc->sfc_pi = NULL;
		}
		if (pl != NULL) {
			m0_layout_put(m0_pdl_to_layout(pl));
			sfc->sfc_pdlayout = NULL;
		}
	}

	if (M0_FI_ENABLED("ut_layout_fsize_fetch")) {
		rc = file_size_and_layout_fetch(it);
		if (rc == 0) {
			rc = m0_sns_cm_fid_layout_instance(sfc->sfc_pdlayout,
					&sfc->sfc_pi, fid);
			if (rc == 0) {
				iter_phase_set(it, ITPH_GROUP_NEXT);
				return M0_RC(rc);
			}
		}
	}
	iter_phase_set(it, ITPH_FID_LOCK);
	return M0_RC(rc);
}

static bool __has_incoming(struct m0_sns_cm *scm, struct m0_pdclust_layout *pl,
			   struct m0_fid *gfid, uint64_t group)
{
	struct m0_cm_ag_id agid;

	M0_PRE(scm != NULL && pl != NULL && gfid != NULL);

	m0_sns_cm_ag_agid_setup(gfid, group, &agid);
	M0_LOG(M0_DEBUG, "agid [%lu] [%lu] [%lu] [%lu]",
	       agid.ai_hi.u_hi, agid.ai_hi.u_lo,
	       agid.ai_lo.u_hi, agid.ai_lo.u_lo);
	return  m0_sns_cm_ag_is_relevant(scm, pl, &agid);

	return false;
}

static bool __group_skip(struct m0_sns_cm_iter *it, uint64_t group)
{
	struct m0_sns_cm           *scm = it2sns(it);
	struct m0_pdclust_layout   *pl = it->si_fc.sfc_pdlayout;
	struct m0_fid              *fid = &it->si_fc.sfc_gob_fid;
	struct m0_fid               cobfid;
	struct m0_pdclust_instance *pi = it->si_fc.sfc_pi;
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	int                         i;

	for (i = 0; i < it->si_fc.sfc_upg; ++i) {
		sa.sa_unit = i;
		sa.sa_group = group;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, fid, &cobfid);
		if (m0_sns_cm_is_cob_failed(scm, &cobfid) &&
		    !m0_sns_cm_is_cob_repaired(scm, &cobfid) &&
		    !m0_sns_cm_unit_is_spare(scm, pl, fid, group, sa.sa_unit))
			return false;
	}

	return true;
}

static int __group_alloc(struct m0_sns_cm *scm, struct m0_fid *gfid,
			 uint64_t group, struct m0_pdclust_layout *pl,
			 bool has_incoming)
{
	struct m0_cm             *cm = &scm->sc_base;
	struct m0_cm_aggr_group  *ag;
	struct m0_cm_ag_id        agid;
	int                       rc;

	m0_sns_cm_ag_agid_setup(gfid, group, &agid);
	/*
	 * Allocate new aggregation group for the given aggregation
	 * group identifier.
	 * Check if the aggregation group has incoming copy packets, if
	 * yes, check if the aggregation group was already created and
	 * processed through sliding window.
	 * Thus if sliding_window_lo < agid < sliding_window_hi then the
	 * group was already processed and we proceed to next group.
	 */
	if (has_incoming) {
		if (m0_cm_ag_id_cmp(&agid,
				    &cm->cm_last_saved_sw_hi) <= 0)
			return -EEXIST;
	}
	if (has_incoming && !cm->cm_ops->cmo_has_space(cm, &agid,
					m0_pdl_to_layout(pl))) {
		M0_LOG(M0_DEBUG, "agid [%lu] [%lu] [%lu] [%lu]",
		       agid.ai_hi.u_hi, agid.ai_hi.u_lo,
		       agid.ai_lo.u_hi, agid.ai_lo.u_lo);
		return M0_RC(-ENOSPC);
	}
	rc = m0_cm_aggr_group_alloc(cm, &agid, has_incoming, &ag);

	return rc;
}

/**
 * Finds parity group having units belonging to the failed container.
 * This iterates through each parity group of the file, and its units.
 * A COB id is calculated for each unit and checked if ti belongs to the
 * failed container, if yes then the group is selected for processing.
 * This is invoked from ITPH_GROUP_NEXT and ITPH_GROUP_NEXT_WAIT phase.
 */
static int __group_next(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm                *scm = it2sns(it);
	struct m0_sns_cm_file_context   *sfc;
	struct m0_pdclust_src_addr      *sa;
	struct m0_fid                   *gfid;
	struct m0_pdclust_layout        *pl;
	uint64_t                         group;
	uint64_t                         nrlu;
	bool                             has_incoming = false;
	int                              rc = 0;

	M0_ENTRY("it = %p", it);

	sfc = &it->si_fc;
	sfc->sfc_groups_nr = m0_sns_cm_nr_groups(sfc->sfc_pdlayout,
						 sfc->sfc_fsize);
	sa = &sfc->sfc_sa;
	gfid = &sfc->sfc_gob_fid;
	pl = sfc->sfc_pdlayout;
	for (group = sa->sa_group; group < sfc->sfc_groups_nr; ++group) {
		M0_ADDB_POST(&m0_addb_gmc, &m0_addb_rt_sns_repair_progress,
			     M0_ADDB_CTX_VEC(&m0_sns_mod_addb_ctx),
			     scm->sc_it.si_total_files, group + 1,
			     sfc->sfc_groups_nr);

		if (__group_skip(it, group))
			continue;
		has_incoming = __has_incoming(scm, pl, gfid, group);
		nrlu = m0_sns_cm_ag_nr_local_units(scm, gfid, pl, group);
		if (has_incoming || nrlu > 0) {
			rc = __group_alloc(scm, gfid, group, pl, has_incoming);
			if (rc != 0) {
				if (rc == -ENOBUFS)
					iter_phase_set(it, ITPH_AG_SETUP);
				if (rc == -ENOSPC)
					rc = -ENOBUFS;
				if (rc == -EEXIST)
					rc = 0;
			}
			sfc->sfc_sa.sa_group = group;
			sfc->sfc_sa.sa_unit = 0;
			if (rc == 0)
				iter_phase_set(it, ITPH_COB_NEXT);
			goto out;
		}
	}

	iter_phase_set(it, ITPH_FID_NEXT);
out:
	return M0_RC(rc);
}

static int iter_group_next_wait(struct m0_sns_cm_iter *it)
{
	return __group_next(it);
}

/**
 * Finds the next parity group to process.
 * @note This operation may block while fetching the file size, as part of file
 * attributes.
 */
static int iter_group_next(struct m0_sns_cm_iter *it)
{
	return __group_next(it);
}

/**
 * Configures aggregation group, acquires buffers for accumulator copy packet
 * in the aggregation group failure contexts.
 *
 * @see struct m0_sns_cm_ag::sag_fc
 * @see struct m0_sns_cm_ag_failure_ctx
 * @see m0_sns_cm_ag_setup()
 */
static int iter_ag_setup(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm                *scm = it2sns(it);
	struct m0_sns_cm_file_context   *sfc = &it->si_fc;
	struct m0_cm_aggr_group         *ag;
	struct m0_sns_cm_ag             *sag;
	struct m0_cm_ag_id               agid;
	bool                             has_incoming = false;
	int                              rc;

	has_incoming = __has_incoming(scm, sfc->sfc_pdlayout, &sfc->sfc_gob_fid, sfc->sfc_sa.sa_group);
	m0_sns_cm_ag_agid_setup(&sfc->sfc_gob_fid, sfc->sfc_sa.sa_group, &agid);
	ag = m0_cm_aggr_group_locate(&scm->sc_base, &agid, has_incoming);
	M0_ASSERT(ag != NULL);
	sag = ag2snsag(ag);
	rc = scm->sc_helpers->sch_ag_setup(sag, sfc->sfc_pdlayout);
	if (rc == 0)
		iter_phase_set(it, ITPH_COB_NEXT);

	return rc;
}

static bool unit_has_data(struct m0_sns_cm *scm, uint32_t unit)
{
	struct m0_sns_cm_file_context *sfc = &scm->sc_it.si_fc;
	struct m0_pdclust_layout      *pl = scm->sc_it.si_fc.sfc_pdlayout;
	enum m0_sns_cm_op              op = scm->sc_op;

	switch(op) {
	case SNS_REPAIR:
		return !sfc->sfc_cob_is_spare_unit;
	case SNS_REBALANCE:
		if (m0_pdclust_unit_classify(pl, unit) == M0_PUT_SPARE)
			return !sfc->sfc_cob_is_spare_unit;
		break;
	default:
		M0_IMPOSSIBLE("Bad operation");
	}

	return false;
}

/**
 * Configures the given copy packet with aggregation group and stob details.
 */
static int iter_cp_setup(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm                *scm = it2sns(it);
	struct m0_cm                    *cm = &scm->sc_base;
	struct m0_pdclust_layout        *pl;
	struct m0_cm_ag_id               agid;
	struct m0_cm_aggr_group         *ag;
	struct m0_sns_cm_file_context   *sfc;
	struct m0_sns_cm_cp             *scp;
	struct m0_fid                   *gfid;
	bool                             has_incoming = false;
	bool                             has_data;
	uint64_t                         group;
	uint64_t                         stob_offset;
	uint64_t                         cp_data_seg_nr;
	uint64_t                         ag_cp_idx;
	int                              rc = 0;

	M0_ENTRY("it = %p", it);

	sfc = &it->si_fc;
	pl = sfc->sfc_pdlayout;
	group = sfc->sfc_sa.sa_group;
	gfid = &sfc->sfc_gob_fid;
	has_incoming = __has_incoming(scm, sfc->sfc_pdlayout, gfid, group);
	has_data = unit_has_data(scm, sfc->sfc_sa.sa_unit - 1);
	if (!has_data)
		goto out;
	m0_sns_cm_ag_agid_setup(gfid, group, &agid);
	ag = m0_cm_aggr_group_locate(cm, &agid, has_incoming);
	M0_ASSERT(ag != NULL);
	stob_offset = sfc->sfc_ta.ta_frame *
		      m0_pdclust_unit_size(sfc->sfc_pdlayout);
	scp = it->si_cp;
	scp->sc_base.c_ag = ag;
	scp->sc_is_local = true;
	cp_data_seg_nr = m0_sns_cm_data_seg_nr(scm, sfc->sfc_pdlayout);
	/*
	 * sfc->sfc_sa.sa_unit has gotten one index ahead. Hence actual
	 * index of the copy packet is (sfc->sfc_sa.sa_unit - 1).
	 * see iter_cob_next().
	 */
	ag_cp_idx = sfc->sfc_sa.sa_unit - 1;
	/*
	 * If the aggregation group unit to be read is a spare unit
	 * containing data then map the spare unit to its corresponding
	 * failed data/parity unit in the aggregation group @ag.
	 * This is required to mark the appropriate data/parity unit of
	 * which this spare contains data.
	 */
	if (m0_pdclust_unit_classify(pl, ag_cp_idx) == M0_PUT_SPARE) {
		rc = m0_sns_repair_data_map(cm->cm_pm, gfid, pl, group,
					    ag_cp_idx, &ag_cp_idx);
		if (rc != 0)
			return M0_RC(rc);
	}
	rc = m0_sns_cm_cp_setup(scp, &sfc->sfc_cob_fid, stob_offset,
				cp_data_seg_nr, ~0, ag_cp_idx);
	if (rc < 0)
		return M0_RC(rc);

	rc = M0_FSO_AGAIN;
out:
	iter_phase_set(it, ITPH_COB_NEXT);

	return M0_RC(rc);
}

/**
 * Finds next local COB corresponding to a unit in the parity group to perform
 * read/write. For each unit in the given parity group, it calculates its
 * corresponding COB fid, and checks if the COB is local. If no local COB is
 * found for a given parity group after iterating through all its units, next
 * parity group is calculated, else the pre-allocated copy packet is populated
 * with required stob details details.
 * @see iter_cp_setup()
 * @note cob_next returns COB fid only for local data and parity units in a
 * parity group.
 */
static int iter_cob_next(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm_file_context   *sfc;
	struct m0_fid                   *cob_fid;
	struct m0_pdclust_src_addr      *sa;
	uint32_t                         upg;
	int                              rc = 0;
	M0_ENTRY("it = %p", it);

	sfc = &it->si_fc;
	upg = sfc->sfc_upg;
	sa = &sfc->sfc_sa;

	cob_fid = &sfc->sfc_cob_fid;
	do {
		if (sa->sa_unit >= upg) {
			++it->si_fc.sfc_sa.sa_group;
			iter_phase_set(it, ITPH_GROUP_NEXT);
			return M0_RC(0);
		}
		/*
		 * Calculate COB fid corresponding to the unit and advance
		 * scm->sc_it.si_src::sa_unit to next unit in the parity
		 * group. If this is the last unit in the parity group then
		 * proceed to next parity group in the GOB.
		 */
		unit_to_cobfid(sfc, cob_fid);
		rc = m0_sns_cm_cob_locate(it->si_cob_dom, cob_fid);
		++sa->sa_unit;
	} while (rc == -ENOENT ||
		 m0_sns_cm_is_cob_failed(it2sns(it), cob_fid));

	if (rc == 0)
		iter_phase_set(it, ITPH_CP_SETUP);

	return M0_RC(rc);
}

/**
 * Transitions the data iterator (m0_sns_cm::sc_it) to ITPH_FID_NEXT
 * in-order to find the first GOB and parity group that needs repair.
 */
M0_INTERNAL int iter_idle(struct m0_sns_cm_iter *it)
{
	iter_phase_set(it, ITPH_FID_NEXT);

	return 0;
}

static int (*iter_action[])(struct m0_sns_cm_iter *it) = {
	[ITPH_IDLE]                  = iter_idle,
	[ITPH_COB_NEXT]              = iter_cob_next,
	[ITPH_GROUP_NEXT]            = iter_group_next,
	[ITPH_GROUP_NEXT_WAIT]       = iter_group_next_wait,
	[ITPH_FID_NEXT]              = iter_fid_next,
	[ITPH_FID_LOCK]              = iter_fid_lock,
	[ITPH_FID_LOCK_WAIT]         = iter_fid_lock_wait,
	[ITPH_FID_ATTR_FETCH]        = iter_fid_attr_fetch,
	[ITPH_FID_ATTR_FETCH_WAIT]   = iter_fid_attr_fetch_wait,
	[ITPH_FID_LAYOUT_FETCH]      = iter_fid_layout_fetch,
	[ITPH_FID_LAYOUT_FETCH_WAIT] = iter_fid_layout_fetch_wait,
	[ITPH_CP_SETUP]              = iter_cp_setup,
	[ITPH_AG_SETUP]              = iter_ag_setup,
};

/**
 * Calculates next data object to be re-structured and accordingly populates
 * the given copy packet.
 */
M0_INTERNAL int m0_sns_cm_iter_next(struct m0_cm *cm, struct m0_cm_cp *cp)
{
	struct m0_sns_cm      *scm;
	struct m0_sns_cm_iter *it;
	int                    rc;
	M0_ENTRY("cm = %p, cp = %p", cm, cp);

	M0_PRE(cm != NULL && cp != NULL);

	scm = cm2sns(cm);
	it = &scm->sc_it;
	it->si_cp = cp2snscp(cp);
	do {
		rc = iter_action[iter_phase(it)](it);
		M0_ASSERT(iter_invariant(it));
	} while (rc == 0);

	if (rc == -ENODATA)
		iter_phase_set(it, ITPH_IDLE);

	return M0_RC(rc);
}

static struct m0_sm_state_descr cm_iter_sd[ITPH_NR] = {
	[ITPH_IDLE] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "iter init",
		.sd_allowed = M0_BITS(ITPH_FID_NEXT, ITPH_FINI)
	},
	[ITPH_COB_NEXT] = {
		.sd_flags   = 0,
		.sd_name    = "COB next",
		.sd_allowed = M0_BITS(ITPH_GROUP_NEXT, ITPH_CP_SETUP, ITPH_IDLE)
	},
	[ITPH_GROUP_NEXT] = {
		.sd_flags   = 0,
		.sd_name    = "group next",
		.sd_allowed = M0_BITS(ITPH_GROUP_NEXT_WAIT, ITPH_COB_NEXT,
				      ITPH_AG_SETUP, ITPH_FID_NEXT)
	},
	[ITPH_GROUP_NEXT_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "group next wait",
		.sd_allowed = M0_BITS(ITPH_COB_NEXT, ITPH_FID_NEXT)
	},
	[ITPH_FID_NEXT] = {
		.sd_flags   = 0,
		.sd_name    = "FID next",
		.sd_allowed = M0_BITS(ITPH_FID_ATTR_FETCH, ITPH_GROUP_NEXT,
				      ITPH_FID_LOCK, ITPH_IDLE)
	},
	[ITPH_FID_LOCK] = {
		.sd_flags   = 0,
		.sd_name    = "File lock wait",
		.sd_allowed = M0_BITS(ITPH_FID_ATTR_FETCH, ITPH_FID_LOCK_WAIT)
	},
	[ITPH_FID_LOCK_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "File lock wait",
		.sd_allowed = M0_BITS(ITPH_FID_ATTR_FETCH)
	},
	[ITPH_FID_ATTR_FETCH] = {
		.sd_flags   = 0,
		.sd_name    = "FID next wait",
		.sd_allowed = M0_BITS(ITPH_FID_ATTR_FETCH_WAIT)
	},
	[ITPH_FID_ATTR_FETCH_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "FID next wait",
		.sd_allowed = M0_BITS(ITPH_FID_LAYOUT_FETCH)
	},
	[ITPH_FID_LAYOUT_FETCH] = {
		.sd_flags   = 0,
		.sd_name    = "FID next wait",
		.sd_allowed = M0_BITS(ITPH_FID_LAYOUT_FETCH_WAIT)
	},
	[ITPH_FID_LAYOUT_FETCH_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "FID next wait",
		.sd_allowed = M0_BITS(ITPH_GROUP_NEXT)
	},
	[ITPH_CP_SETUP] = {
		.sd_flags   = 0,
		.sd_name    = "cp setup",
		.sd_allowed = M0_BITS(ITPH_COB_NEXT)
	},
	[ITPH_AG_SETUP] = {
		.sd_flags   = 0,
		.sd_name    = "ag setup",
		.sd_allowed = M0_BITS(ITPH_COB_NEXT)
	},
	[ITPH_FINI] = {
		.sd_flags = M0_SDF_TERMINAL,
		.sd_name  = "cm iter fini",
		.sd_allowed = 0
	}
};

static const struct m0_sm_conf cm_iter_sm_conf = {
	.scf_name      = "sm: cm_iter_conf",
	.scf_nr_states = ARRAY_SIZE(cm_iter_sd),
	.scf_state     = cm_iter_sd
};

static void layout_fini(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm_file_context *sfc;

	M0_PRE(iter_invariant(it));

	sfc = &it->si_fc;
	if (sfc->sfc_pi != NULL) {
		m0_layout_instance_fini(&sfc->sfc_pi->pi_base);
		sfc->sfc_pi = NULL;
	}
	if (sfc->sfc_pdlayout != NULL) {
		m0_layout_put(m0_pdl_to_layout(sfc->sfc_pdlayout));
		sfc->sfc_pdlayout = NULL;
	}
}

M0_INTERNAL int m0_sns_cm_iter_init(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm     *scm = it2sns(it);
	struct m0_cm         *cm;
	int                   rc;


	M0_PRE(it != NULL);

	cm = &scm->sc_base;
	it->si_beseg = cm->cm_service.rs_reqh->rh_beseg;
        rc = m0_ios_cdom_get(cm->cm_service.rs_reqh, &it->si_cob_dom);
        if (rc != 0) {
		SNS_ADDB_FUNCFAIL(rc, &m0_sns_mod_addb_ctx, ITER_CDOM_GET);
                return rc;
	}
	m0_sm_init(&it->si_sm, &cm_iter_sm_conf, ITPH_IDLE, &cm->cm_sm_group);
	m0_sns_cm_iter_bob_init(it);
	it->si_total_fsize = 0;
	it->si_total_files = 0;

	return rc;
}

M0_INTERNAL int m0_sns_cm_iter_start(struct m0_sns_cm_iter *it)
{
	/*
	 * Pick the best possible fid to initialise the namespace iter.
	 * m0t1fs starts its fid space from {0,4}.
	 * XXX This should be changed to {0, 0} once multiple cob domains
	 * are added per service.
	 */
	struct m0_fid         gfid = {0, 4};
	int                   rc;

	M0_PRE(it != NULL);
	M0_PRE(iter_phase(it) == ITPH_IDLE);

	rc = m0_cob_ns_iter_init(&it->si_cns_it, &gfid, it->si_cob_dom);
	if (rc == 0)
		M0_SET0(&it->si_fc);

	return rc;
}

M0_INTERNAL void m0_sns_cm_iter_stop(struct m0_sns_cm_iter *it)
{
	M0_PRE(iter_phase(it) == ITPH_IDLE);

	m0_cob_ns_iter_fini(&it->si_cns_it);
	layout_fini(it);
}

M0_INTERNAL void m0_sns_cm_iter_fini(struct m0_sns_cm_iter *it)
{
	M0_PRE(it != NULL);
	M0_PRE(iter_phase(it) == ITPH_IDLE);

	iter_phase_set(it, ITPH_FINI);
	m0_sm_fini(&it->si_sm);
	m0_sns_cm_iter_bob_fini(it);
}

/** @} SNSCM */
#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
