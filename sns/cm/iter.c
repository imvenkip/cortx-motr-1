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
#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/trace.h"
#include "lib/misc.h"
#include "lib/cdefs.h"
#include "lib/finject.h"

#include "cob/cob.h"
#include "mdstore/mdstore.h"
#include "reqh/reqh.h"
#include "ioservice/io_service.h"

#include "cm/proxy.h"
#include "sns/cm/cm.h"
#include "sns/cm/cp.h"
#include "sns/cm/ag.h"
#include "sns/cm/cm_utils.h"

/**
  @addtogroup SNSCM

  @{
*/

enum {
	/*
	 * Temporary default layout identifier for SNS Repair copy machine.
	 * @todo Remove this and fetch layout id as part of file attributes.
	 */
	SNS_DEFAULT_LAYOUT_ID = 0xAC1DF00D,
	SNS_DEFAULT_NR_DATA_UNITS = 2,
	SNS_DEFAULT_NR_PARITY_UNITS  = 1,
	SNS_DEFAULT_POOL_WIDTH = SNS_DEFAULT_NR_DATA_UNITS +
				 2 * SNS_DEFAULT_NR_PARITY_UNITS,
	/*
	 * XXX SNS_FILE_SIZE is temporary hard coded file size used for
	 * sns repair. Eventually this should be retrieved a part of file
	 * attributes, once set_attr() and get_attr() interfaces are
	 * implemented.
	 */
	SNS_FILE_SIZE_DEFAULT = 1 << 16
};

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
	/**
	 * Iterator waits in this phase after performing a blocking operation in
	 * ITPH_FID_NEXT (i.e. fetch file layout) for completion event.
	 */
	ITPH_FID_NEXT_WAIT,
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

enum {
	IT_WAIT = M0_FSO_WAIT
};

M0_INTERNAL struct m0_sns_cm *it2sns(struct m0_sns_cm_iter *it)
{
	return container_of(it, struct m0_sns_cm, sc_it);
}

/*
M0_INTERNAL uint64_t m0_sns_cm_iter_failures_nr(const struct m0_sns_cm_iter *it)
{
	return it->si_fc.sfc_group_nr_fail_units;
}
*/

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
	}

	return rc;
}

static bool unit_is_spare(const struct m0_pdclust_layout *pl, int unit)
{
	return m0_pdclust_unit_classify(pl, unit) == M0_PUT_SPARE;
}

/**
 * Calculates COB fid for m0_sns_cm_file_context::sfc_sa.
 * Saves calculated struct m0_pdclust_tgt_addr in
 * m0_sns_cm_file_context::sfc_ta.
 */
static void unit_to_cobfid(struct m0_sns_cm_file_context *sfc,
			   struct m0_fid *cob_fid_out)
{
	struct m0_pdclust_instance  *pi;
	struct m0_pdclust_layout    *pl;
	struct m0_pdclust_src_addr  *sa;
	struct m0_pdclust_tgt_addr  *ta;
	struct m0_fid               *fid;

	fid = &sfc->sfc_gob_fid;
	pi = sfc->sfc_pi;
	pl = sfc->sfc_pdlayout;
	sa = &sfc->sfc_sa;
	ta = &sfc->sfc_ta;
	sfc->sfc_cob_is_spare_unit = unit_is_spare(pl, sa->sa_unit);
	m0_sns_cm_unit2cobfid(pl, pi, sa, ta, fid, cob_fid_out);
}

static int iter_fid_next_wait(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm_file_context *sfc = &it->si_fc;
	struct m0_pdclust_layout      *pl  = sfc->sfc_pdlayout;
	struct m0_fid                 *fid = &sfc->sfc_gob_fid;
	int                            rc;

	rc = m0_sns_cm_fid_layout_instance(pl, &sfc->sfc_pi, fid);
	if (rc == 0) {
		sfc->sfc_sa.sa_group = 0;
		sfc->sfc_sa.sa_unit = 0;
		iter_phase_set(it, ITPH_GROUP_NEXT);
	}

	return rc;
}

/* Uses name space iterator. */
M0_INTERNAL int __fid_next(struct m0_sns_cm_iter *it, struct m0_fid *fid_next)
{
	int             rc;
	struct m0_db_tx tx;

	rc = m0_db_tx_init(&tx, it->si_dbenv, 0);
	if (rc != 0)
		return rc;

	rc = m0_cob_ns_iter_next(&it->si_cns_it, &tx, fid_next);
        if (rc == 0)
                m0_db_tx_commit(&tx);
        else
                m0_db_tx_abort(&tx);

	return rc;
}

/**
 * Fetches next GOB fid.
 * @note Presently uses a hard coded GOB fid for single file repair.
 * @todo Use name space iterator to fetch next GOB fid.
 */
static int iter_fid_next(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm_file_context   *sfc = &it->si_fc;
	struct m0_pdclust_layout        *pl  = sfc->sfc_pdlayout;
	struct m0_fid                   *fid = &sfc->sfc_gob_fid;
	struct m0_fid                    fid_next = {0, 0};
	int                              rc;

	/* Get current GOB fid saved in the iterator. */
	do {
		rc = __fid_next(it, &fid_next);
	} while (rc == 0 && (m0_fid_eq(&fid_next, &M0_COB_ROOT_FID)     ||
			     m0_fid_eq(&fid_next, &M0_COB_SESSIONS_FID) ||
			     m0_fid_eq(&fid_next, &M0_COB_SLASH_FID)));

	if (rc == -ENOENT)
		return -ENODATA;
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

		rc = file_size_and_layout_fetch(it);
		if (rc < 0 && M0_FI_ENABLED("layout_fetch_error_as_done"))
			return -ENODATA;
		if (rc < 0)
			return rc;

		if (rc == IT_WAIT) {
			iter_phase_set(it, ITPH_FID_NEXT_WAIT);
			return rc;
		}
		pl = sfc->sfc_pdlayout;
		rc = m0_sns_cm_fid_layout_instance(pl, &sfc->sfc_pi, fid);
		if (rc == 0) {
			sfc->sfc_sa.sa_group = 0;
			sfc->sfc_sa.sa_unit = 0;
			iter_phase_set(it, ITPH_GROUP_NEXT);
		}
	}

	return rc;
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
	int                         fnr_cnt = 0;
	int                         i;

	for (i = 0; i < scm->sc_failures_nr; ++i) {
		sa.sa_unit = m0_pdclust_N(pl) + m0_pdclust_K(pl) - i;
		sa.sa_group = group;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, fid, &cobfid);
		if (m0_sns_cm_is_cob_failed(scm, &cobfid))
			M0_CNT_INC(fnr_cnt);
	}

	return fnr_cnt == scm->sc_failures_nr;
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
	uint64_t                         group;
	uint32_t                         group_fnr;

	sfc = &it->si_fc;
	sfc->sfc_groups_nr = m0_sns_cm_nr_groups(sfc->sfc_pdlayout,
						 sfc->sfc_fsize);
	sa = &sfc->sfc_sa;
	for (group = sa->sa_group; group < sfc->sfc_groups_nr; ++group) {
		group_fnr = 0;
		if (__group_skip(it, group))
			continue;
		group_fnr = m0_sns_cm_ag_failures_nr(scm, &sfc->sfc_gob_fid,
						     sfc->sfc_pdlayout,
						     sfc->sfc_pi, group);
		if (group_fnr > 0){
			sfc->sfc_sa.sa_group = group;
			sfc->sfc_sa.sa_unit =
				m0_sns_cm_ag_unit_start(scm->sc_op,
							sfc->sfc_pdlayout);
			iter_phase_set(it, ITPH_COB_NEXT);
			goto out;
		}
	}

	iter_phase_set(it, ITPH_FID_NEXT);
out:
	return 0;
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

static bool __has_incoming(struct m0_sns_cm *scm, struct m0_pdclust_layout *pl,
			   struct m0_cm_ag_id *id)
{
	M0_PRE(scm != NULL && pl != NULL && id != NULL);

	if (scm->sc_base.cm_proxy_nr > 0)
		return  m0_sns_cm_ag_is_relevant(scm, pl, id);

	return false;
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

	m0_sns_cm_ag_agid_setup(&sfc->sfc_gob_fid, sfc->sfc_sa.sa_group, &agid);
	has_incoming = __has_incoming(scm, sfc->sfc_pdlayout, &agid);
	ag = m0_cm_aggr_group_locate(&scm->sc_base, &agid,
				     has_incoming);
	M0_ASSERT(ag != NULL);
	sag = ag2snsag(ag);
	rc = m0_sns_cm_ag_setup(sag, sfc->sfc_pdlayout);
	if (rc == 0)
		iter_phase_set(it, ITPH_CP_SETUP);

	return rc;
}

/**
 * Configures the given copy packet with aggregation group and stob details.
 */
static int iter_cp_setup(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm                *scm = it2sns(it);
	struct m0_cm_ag_id               agid;
	struct m0_cm_aggr_group         *ag;
	struct m0_sns_cm_file_context   *sfc;
	struct m0_sns_cm_cp             *scp;
	bool                             has_incoming = false;
	uint64_t                         stob_offset;
	uint64_t                         cp_data_seg_nr;
	enum m0_sns_cm_op                op = scm->sc_op;
	int                              rc = 0;

	sfc = &it->si_fc;
	m0_sns_cm_ag_agid_setup(&sfc->sfc_gob_fid, sfc->sfc_sa.sa_group, &agid);
	has_incoming = __has_incoming(scm, sfc->sfc_pdlayout, &agid);
	ag = m0_cm_aggr_group_locate(&scm->sc_base, &agid, has_incoming);
	if (ag == NULL) {
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
			if (m0_cm_ag_id_cmp(&agid, &scm->sc_base.cm_last_saved_sw_hi) <= 0)
				goto out;
		}
		if (has_incoming && !m0_sns_cm_has_space(&scm->sc_base, &agid,
							 sfc->sfc_pdlayout)) {
			M0_LOG(M0_FATAL, "agid [%lu] [%lu] [%lu] [%lu]",
				agid.ai_hi.u_hi, agid.ai_hi.u_lo, agid.ai_lo.u_hi, agid.ai_lo.u_lo);
				return -ENOBUFS;
		}
		rc = m0_cm_aggr_group_alloc(&scm->sc_base, &agid,
					    has_incoming, &ag);
		if (rc != 0) {
			if (rc == -ENOBUFS)
				iter_phase_set(it, ITPH_AG_SETUP);
			return rc;
		}
	}

	if ((!sfc->sfc_cob_is_spare_unit && op == SNS_REPAIR) ||
	    (sfc->sfc_cob_is_spare_unit && op == SNS_REBALANCE)) {
		stob_offset = sfc->sfc_ta.ta_frame *
			      m0_pdclust_unit_size(sfc->sfc_pdlayout);
		scp = it->si_cp;
		scp->sc_base.c_ag = ag;
		scp->sc_is_local = true;
		cp_data_seg_nr = m0_sns_cm_data_seg_nr(scm, sfc->sfc_pdlayout);
		/*
		 * sfc->sfc_sa.sa_unit has gotten one index ahead. Hence actual
		 * index of the copy packet is (sfc->sfc_sa.sa_unit - 1).
		 */
		rc = m0_sns_cm_cp_setup(scp, &sfc->sfc_cob_fid, stob_offset,
					cp_data_seg_nr, sfc->sfc_sa.sa_unit - 1);
		if (rc < 0)
			return rc;

		rc = M0_FSO_AGAIN;
	}
out:
	iter_phase_set(it, ITPH_COB_NEXT);

	return rc;
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

	sfc = &it->si_fc;
	upg = sfc->sfc_upg;
	sa = &sfc->sfc_sa;

	cob_fid = &sfc->sfc_cob_fid;
	do {
		if (sa->sa_unit >= upg) {
			++it->si_fc.sfc_sa.sa_group;
			iter_phase_set(it, ITPH_GROUP_NEXT);
			return 0;
		}
		/*
		 * Calculate COB fid corresponding to the unit and advance
		 * scm->sc_it.si_src::sa_unit to next unit in the parity
		 * group. If this is the last unit in the parity group then
		 * proceed to next parity group in the GOB.
		 */
		unit_to_cobfid(sfc, cob_fid);
		rc = m0_sns_cm_cob_locate(it->si_dbenv, it->si_cob_dom, cob_fid);
		++sa->sa_unit;
	} while (rc == -ENOENT ||
		 m0_sns_cm_is_cob_failed(it2sns(it), cob_fid));

	if (rc == 0)
		iter_phase_set(it, ITPH_CP_SETUP);

	return rc;
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
	[ITPH_IDLE]            = iter_idle,
	[ITPH_COB_NEXT]        = iter_cob_next,
	[ITPH_GROUP_NEXT]      = iter_group_next,
	[ITPH_GROUP_NEXT_WAIT] = iter_group_next_wait,
	[ITPH_FID_NEXT]        = iter_fid_next,
	[ITPH_FID_NEXT_WAIT]   = iter_fid_next_wait,
	[ITPH_CP_SETUP]        = iter_cp_setup,
	[ITPH_AG_SETUP]        = iter_ag_setup,
};

/**
 * Calculates next data object to be re-structured and accordingly populates
 * the given copy packet.
 */
M0_INTERNAL int m0_sns_cm_iter_next(struct m0_cm *cm, struct m0_cm_cp *cp)
{
	struct m0_sns_cm *scm;
	int               rc;

	M0_PRE(cm != NULL && cp != NULL);

	scm = cm2sns(cm);
	scm->sc_it.si_cp = cp2snscp(cp);
	do {
		rc = iter_action[iter_phase(&scm->sc_it)](&scm->sc_it);
		M0_ASSERT(iter_invariant(&scm->sc_it));
	} while (rc == 0);

	return rc;
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
				      ITPH_FID_NEXT)
	},
	[ITPH_GROUP_NEXT_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "group next wait",
		.sd_allowed = M0_BITS(ITPH_COB_NEXT, ITPH_FID_NEXT)
	},
	[ITPH_FID_NEXT] = {
		.sd_flags   = 0,
		.sd_name    = "FID next",
		.sd_allowed = M0_BITS(ITPH_FID_NEXT_WAIT, ITPH_GROUP_NEXT,
				      ITPH_IDLE)
	},
	[ITPH_FID_NEXT_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "FID next wait",
		.sd_allowed = M0_BITS(ITPH_GROUP_NEXT)
	},
	[ITPH_CP_SETUP] = {
		.sd_flags   = 0,
		.sd_name    = "cp setup",
		.sd_allowed = M0_BITS(ITPH_AG_SETUP, ITPH_COB_NEXT)
	},
	[ITPH_AG_SETUP] = {
		.sd_flags   = 0,
		.sd_name    = "ag setup",
		.sd_allowed = M0_BITS(ITPH_CP_SETUP)
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
	it->si_dbenv = cm->cm_service.rs_reqh->rh_dbenv;
        rc = m0_ios_cdom_get(cm->cm_service.rs_reqh, &it->si_cob_dom);
        if (rc != 0)
                return rc;
	m0_sm_init(&it->si_sm, &cm_iter_sm_conf, ITPH_IDLE, &cm->cm_sm_group);
	m0_sns_cm_iter_bob_init(it);

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

	rc = m0_cob_ns_iter_init(&it->si_cns_it, &gfid, it->si_dbenv, it->si_cob_dom);
	if (rc == 0)
		M0_SET0(&it->si_fc.sfc_gob_fid);

	return rc;
}

M0_INTERNAL void m0_sns_cm_iter_stop(struct m0_sns_cm_iter *it)
{
	iter_phase_set(it, ITPH_IDLE);
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

#undef M0_TRACE_SUBSYSTEM

/** @} SNSCM */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
