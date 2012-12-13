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
#include "layout/pdclust.h"
#include "reqh/reqh.h"
#include "ioservice/io_service.h"
#include "mdstore/mdstore.h"
#include "sns/cm/cm.h"
#include "sns/cm/cp.h"
#include "sns/cm/ag.h"
#include "sns/cm/st/trigger_fom.h"

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

/**
 * Default hard coded file fid for a single file repair.
 * This will be removed and name space iterator mechanism will be used.
 */
static const struct m0_fid default_single_file_fid = {
	.f_container = 0,
	.f_key = 4
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
	ITPH_INIT,
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

static uint64_t __unit_start(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm *scm = it2sns(it);

	switch (scm->sc_op) {
	case SNS_REPAIR:
		/* Start from 0th unit of the group. */
		return 0;
	case SNS_REBALANCE:
		/* Start from the first spare unit of the group. */
		return it->si_pl.spl_N;
	default:
		 M0_IMPOSSIBLE("op");
	}
}

static uint64_t __unit_end(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm *scm = it2sns(it);

	switch (scm->sc_op) {
	case SNS_REPAIR:
		/* End at the last data/parity unit of the group. */
		return it->si_pl.spl_dpupg;
	case SNS_REBALANCE:
		/* End at the last spare unit of the group. */
		return it->si_pl.spl_upg;
	default:
		M0_IMPOSSIBLE("op");
	}
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
                      const struct m0_sns_cm_pdclust_layout *spl)
{
	return ergo(M0_IN(phase, (ITPH_COB_NEXT, ITPH_GROUP_NEXT,
				 ITPH_GROUP_NEXT_WAIT, ITPH_CP_SETUP)),
		   spl->spl_base != NULL && spl->spl_le != NULL &&
		   spl->spl_pi != NULL && spl->spl_N != 0 && spl->spl_K != 0 &&
		   spl->spl_P != 0 && spl->spl_upg != 0 &&
		   spl->spl_P >= (spl->spl_N + 2 * spl->spl_K) &&
		   spl->spl_dpupg != 0 && m0_fid_is_set(&spl->spl_gob_fid)) &&
	       ergo(M0_IN(phase, (ITPH_CP_SETUP)), spl->spl_groups_nr != 0 &&
		   m0_fid_is_set(&spl->spl_cob_fid) &&
		   spl->spl_sa.sa_group <= spl->spl_groups_nr &&
		   spl->spl_sa.sa_unit <= spl->spl_upg &&
		   spl->spl_ta.ta_obj <= spl->spl_P);
}

static bool iter_invariant(const struct m0_sns_cm_iter *it)
{
	enum cm_data_iter_phase phase = iter_phase(it);

	return it != NULL && m0_sns_cm_iter_bob_check(it) &&
	       it->si_cp != NULL && iter_layout_invariant(phase, &it->si_pl);
}

/**
 * Fetches total size of a file corresponding to the given GOB fid
 * (m0_sns_cm::si_pl::spl_gob_fid). This is used to calculate
 * total number of parity groups per GOB.
 * Returns 0 * @todo Currently the file size is hard coded, but eventually it would be
 * retrieved as part of file attributes.
 * @note Fetching file attributes may block.
 * @retval 0 on success, IT_WAIT for blocking operation
 */
static ssize_t file_size(struct m0_sns_cm_iter *it)
{
	M0_PRE(it != NULL);

	rcm->rc_it.ri_pl.rpl_fsize = m0_trigger_file_size_get(&rcm->rc_it.
							      ri_pl.
							      rpl_gob_fid);
	return 0;
}

/**
 * Fetches file layout for scm->sc_it.si_pl.spl_gob_fid.
 * @todo To fetch file layout as part of file attributes.
 * @note This may block.
 * @retval 0 on success, IT_WAIT for blocking operation
 */
static int cm_layout_fetch(struct m0_sns_cm_iter *it)
{
	M0_PRE(it != NULL);

	return 0;
}

/*
 * Searches for given cob_fid in the local cob domain.
 */
static int cob_locate(const struct m0_sns_cm_iter *it,
		      const struct m0_fid *cob_fid)
{
	struct m0_cob        *cob;
	struct m0_cob_oikey   oikey;
	struct m0_db_tx       tx;
	struct m0_dbenv      *dbenv;
	struct m0_cob_domain *cdom;
	int                   rc;

	dbenv = it->si_dbenv;
	cdom  = it->si_cob_dom;
	M0_ASSERT(cdom != NULL);

	rc = m0_db_tx_init(&tx, dbenv, 0);
	if (rc != 0)
		return rc;
	m0_cob_oikey_make(&oikey, cob_fid, 0);
	rc = m0_cob_locate(cdom, &oikey, 0, &cob, &tx);
	if (rc == 0) {
		M0_ASSERT(m0_fid_eq(cob_fid, cob->co_fid));
		m0_db_tx_commit(&tx);
	} else
		m0_db_tx_abort(&tx);

	return rc;
}

static bool unit_is_spare(const struct m0_pdclust_layout *pl, int unit)
{
	return m0_pdclust_unit_classify(pl, unit) == M0_PUT_SPARE;
}

/*
 * Returns cob fid for the sa->sa_unit.
 * @see m0_pdclust_instance_map
 */
static void __unit_to_cobfid(struct m0_pdclust_layout *pl,
			     struct m0_pdclust_instance *pi,
			     const struct m0_pdclust_src_addr *sa,
			     struct m0_pdclust_tgt_addr *ta,
			     const struct m0_fid *gfid, struct m0_fid *cfid_out)
{
	struct m0_layout_enum *le;

	m0_pdclust_instance_map(pi, sa, ta);
	le = m0_layout_to_enum(m0_pdl_to_layout(pl));
	m0_layout_enum_get(le, ta->ta_obj, gfid, cfid_out);
}

M0_INTERNAL uint64_t nr_local_units(struct m0_sns_cm *scm,
				    const struct m0_fid *fid, uint64_t group)
{
	struct m0_sns_cm_iter           *it  = &scm->sc_it;
	struct m0_sns_cm_pdclust_layout *spl = &it->si_pl;
	struct m0_pdclust_src_addr       sa;
	struct m0_pdclust_tgt_addr       ta;
	struct m0_fid                    cobfid;
	uint64_t                         nrlu = 0;
	int                              rc;
	int                              i;
	int                              start = __unit_start(it);
	int                              end   = __unit_end(it);

	M0_PRE(iter_invariant(it));
	M0_PRE(m0_fid_eq(fid, &spl->spl_gob_fid));

	sa.sa_group = group;
	for (i = start; i < end; ++i) {
		sa.sa_unit = i;
		M0_SET0(&ta);
		M0_SET0(&cobfid);
		__unit_to_cobfid(spl->spl_base, spl->spl_pi, &sa, &ta, fid,
				 &cobfid);
		rc = cob_locate(it, &cobfid);
		if (rc == 0 && cobfid.f_container != it->si_fdata)
			++nrlu;
	}

	return nrlu;
}

/**
 * Returns the index of the failed data/parity unit in the parity group.
 * The same offset of the data/parity unit in the group on the failed device is
 * used to copy data from the spare unit to the new device by re-balance
 * operation.
 */
static uint64_t __group_failed_unit_index(const struct m0_sns_cm_pdclust_layout
					  *spl, uint64_t group, uint64_t fdata)
{
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	const struct m0_fid        *gobfid = &spl->spl_gob_fid;
	struct m0_fid               cobfid;
	int                         i;

	sa.sa_group = group;
	for (i = 0; i < spl->spl_dpupg; ++i) {
		sa.sa_unit = i;
		M0_SET0(&ta);
		M0_SET0(&cobfid);
		__unit_to_cobfid(spl->spl_base, spl->spl_pi, &sa, &ta, gobfid,
				 &cobfid);
		if (cobfid.f_container == fdata)
			return i;
	}

	return ~0;
}

/**
 * Returns index of spare unit in the parity group.
 */
static uint64_t __spare_unit_nr(const struct m0_sns_cm_pdclust_layout *spl,
				uint64_t group)
{
	return spl->spl_N + spl->spl_K + 1 - 1;
}

static uint64_t __target_unit_nr(const struct m0_sns_cm_pdclust_layout *spl,
				 uint64_t group, uint64_t fdata,
				 enum m0_sns_cm_op op)
{
	switch (op) {
	case SNS_REPAIR:
		return __spare_unit_nr(spl, group);
	case SNS_REBALANCE:
		return __group_failed_unit_index(spl, group, fdata);
	default:
		 M0_IMPOSSIBLE("op");
	}
}

M0_INTERNAL void target_unit_to_cob(struct m0_sns_cm_ag *sag)
{
	struct m0_sns_cm                *scm = cm2sns(sag->sag_base.cag_cm);
	struct m0_sns_cm_pdclust_layout *spl = &scm->sc_it.si_pl;
	struct m0_pdclust_src_addr       sa;
	struct m0_pdclust_tgt_addr       ta;
	struct m0_fid                    gobfid;
	struct m0_fid                    cobfid;


	M0_SET0(&ta);
	M0_SET0(&cobfid);
	agid2fid(&sag->sag_base, &gobfid);
	M0_ASSERT(m0_fid_eq(&gobfid, &spl->spl_gob_fid));
	sa.sa_group = agid2group(&sag->sag_base);
	sa.sa_unit  = __target_unit_nr(spl, sa.sa_group, scm->sc_it.si_fdata,
				       scm->sc_op);
	__unit_to_cobfid(spl->spl_base, spl->spl_pi, &sa, &ta, &gobfid,
			 &cobfid);
	sag->sag_tgt_cobfid = cobfid;
	sag->sag_tgt_cob_index = ta.ta_frame *
				 m0_pdclust_unit_size(spl->spl_base);
}

/**
 * Calculates COB fid for m0_sns_cm_iter::si_pl::si_sa.
 * Saves calculated struct m0_pdclust_tgt_addr in
 * m0_sns_cm_pdclust_layout::spl_ta.
 */
static void unit_to_cobfid(struct m0_sns_cm_pdclust_layout *spl,
			   struct m0_fid *cob_fid_out)
{
	struct m0_pdclust_instance  *pi;
	struct m0_pdclust_layout    *pl;
	struct m0_pdclust_src_addr  *sa;
	struct m0_pdclust_tgt_addr  *ta;
	struct m0_fid               *fid;

	fid = &spl->spl_gob_fid;
	pi = spl->spl_pi;
	pl = spl->spl_base;
	sa = &spl->spl_sa;
	ta = &spl->spl_ta;
	spl->spl_cob_is_spare_unit = unit_is_spare(pl, sa->sa_unit);
	__unit_to_cobfid(pl, pi, sa, ta, fid, cob_fid_out);
}

/**
 * Builds layout instance for new GOB fid calculated in ITPH_FID_NEXT phase.
 * @see iter_fid_next()
 */
static int fid_layout_build(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm_pdclust_layout *spl;
	struct m0_pdclust_layout        *pl;
	struct m0_layout_instance       *li;
	struct m0_fid                   *fid;
	int                              rc;

	spl = &it->si_pl;
	fid = &spl->spl_gob_fid;
	pl  = spl->spl_base;
        /* Destroy previous pdclust instance */
	if (spl->spl_pi != NULL)
		m0_layout_instance_fini(&spl->spl_pi->pi_base);
	rc = m0_layout_instance_build(&pl->pl_base.sl_base, fid, &li);
	if (rc == 0) {
		spl->spl_pi = m0_layout_instance_to_pdi(li);
		spl->spl_sa.sa_group = 0;
		spl->spl_sa.sa_unit = 0;
		iter_phase_set(it, ITPH_GROUP_NEXT);
	}

	return rc;
}

static int iter_fid_next_wait(struct m0_sns_cm_iter *it)
{
	return fid_layout_build(it);
}

/* Uses name space iterator. */
static int __fid_next(struct m0_sns_repair_cm *rcm, struct m0_fid *fid_next)
{
	int             rc;
	struct m0_db_tx tx;

	rc = m0_db_tx_init(&tx, rcm->rc_base.cm_service.rs_reqh->rh_dbenv, 0);
	if (rc != 0)
		return rc;

	rc = m0_cob_ns_iter_next(&rcm->rc_it.ri_cns_it, &tx, fid_next);
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
	struct m0_fid fid_next = {0, 0};
	int           rc;

	/* Get current GOB fid saved in the iterator. */
	rc = __fid_next(rcm, &fid_next);
	if (rc == -ENOENT)
		return -ENODATA;
	if (rc == 0) {
		/* Save next GOB fid in the iterator. */
		rcm->rc_it.ri_pl.rpl_gob_fid = fid_next;
		rc = cm_layout_fetch(it);
		if (rc == IT_WAIT) {
			iter_phase_set(it, ITPH_FID_NEXT_WAIT);
			return rc;
		}
		rc = fid_layout_build(it);
	}

	return rc;
}

static uint64_t nr_groups(struct m0_sns_cm_pdclust_layout *spl)
{
	uint64_t nr_data_bytes_per_group;

	nr_data_bytes_per_group =  m0_pdclust_N(spl->spl_base) *
				   m0_pdclust_unit_size(spl->spl_base);
	return spl->spl_fsize % nr_data_bytes_per_group ?
	       spl->spl_fsize / nr_data_bytes_per_group + 1 :
	       spl->spl_fsize / nr_data_bytes_per_group;
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
	struct m0_sns_cm_pdclust_layout *spl;
	struct m0_pdclust_src_addr      *sa;
	struct m0_fid                    cob_fid;
	uint64_t                         groups_nr;
	uint64_t                         group;
	uint64_t                         unit;

	spl = &it->si_pl;
	spl->spl_groups_nr = groups_nr = nr_groups(spl);
	sa = &spl->spl_sa;
	for (group = sa->sa_group; group < groups_nr; ++group) {
		for (unit = 0; unit < spl->spl_dpupg; ++unit) {
			spl->spl_sa.sa_unit = unit;
			spl->spl_sa.sa_group = group;
			unit_to_cobfid(spl, &cob_fid);
			if (cob_fid.f_container == it->si_fdata) {
				spl->spl_sa.sa_unit = __unit_start(it);
				iter_phase_set(it, ITPH_COB_NEXT);
				goto out;
			}
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
	int rc;

	/* File size may have changed, fetch new file size. */
	rc = file_size(it);
	if (rc == IT_WAIT) {
		iter_phase_set(it, ITPH_GROUP_NEXT_WAIT);
		return rc;
	}
	rc = __group_next(it);

	return rc;
}

static int cm_buf_attach(struct m0_sns_cm *scm, struct m0_cm_cp *cp)
{
	struct m0_net_buffer *buf;
	size_t                colour;

	colour =  cp_home_loc_helper(cp) % scm->sc_obp.nbp_colours_nr;
	buf = m0_sns_cm_buffer_get(&scm->sc_obp, colour);
	if (buf == NULL)
		return -ENOBUFS;
	cp->c_data = &buf->nb_buffer;

	return 0;
}

static void agid_setup(const struct m0_fid *gob_fid, uint64_t group,
		       struct m0_cm_ag_id *agid)
{
	agid->ai_hi.u_hi = gob_fid->f_container;
	agid->ai_hi.u_lo = gob_fid->f_key;
	agid->ai_lo.u_hi = 0;
	agid->ai_lo.u_lo = group;
}

static void __cp_setup(struct m0_sns_cm_cp *rcp,
		       const struct m0_fid *cob_fid, uint64_t stob_offset,
		       struct m0_cm_aggr_group *ag)
{
	rcp->sc_sid.si_bits.u_hi = cob_fid->f_container;
	rcp->sc_sid.si_bits.u_lo = cob_fid->f_key;
	rcp->sc_index = stob_offset;
	rcp->sc_base.c_ag = ag;
}

/**
 * Configures the given copy packet with aggregation group and stob details.
 */
static int iter_cp_setup(struct m0_sns_cm_iter *it)
{
	struct m0_cm_ag_id               agid;
	struct m0_sns_cm_ag             *rag;
	struct m0_sns_cm                *scm = it2sns(it);
	struct m0_sns_cm_pdclust_layout *spl;
	struct m0_sns_cm_cp             *rcp;
	uint64_t                         stob_offset;
	enum m0_sns_cm_op                op = scm->sc_op;
	int                              rc = 0;

	spl = &it->si_pl;
	agid_setup(&spl->spl_gob_fid, spl->spl_sa.sa_group, &agid);
	rag = m0_sns_cm_ag_find(scm, &agid);

	if (rag == NULL)
		return -EINVAL;
	if ((!spl->spl_cob_is_spare_unit && op == SNS_REPAIR) ||
	    (spl->spl_cob_is_spare_unit && op == SNS_REBALANCE)) {
		stob_offset = spl->spl_ta.ta_frame *
			      m0_pdclust_unit_size(spl->spl_base);
		rcp = it->si_cp;
		__cp_setup(rcp, &spl->spl_cob_fid, stob_offset, &rag->sag_base);
		rc = cm_buf_attach(scm, &rcp->sc_base);
		if (rc < 0)
			return rc;
		rc = M0_FSO_AGAIN;
	}

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
	struct m0_sns_cm_pdclust_layout *spl;
	struct m0_fid                   *cob_fid;
	struct m0_pdclust_src_addr      *sa;
	struct m0_pdclust_layout        *pl;
	uint32_t                         upg;
	int                              rc = 0;

	spl = &it->si_pl;
	pl = spl->spl_base;
	upg = spl->spl_upg;
	sa = &spl->spl_sa;

	cob_fid = &spl->spl_cob_fid;
	do {
		if (sa->sa_unit >= upg) {
			++it->si_pl.spl_sa.sa_group;
			iter_phase_set(it, ITPH_GROUP_NEXT);
			return 0;
		}
		/*
		 * Calculate COB fid corresponding to the unit and advance
		 * scm->sc_it.si_src::sa_unit to next unit in the parity
		 * group. If this is the last unit in the parity group then
		 * proceed to next parity group in the GOB.
		 */
		unit_to_cobfid(spl, cob_fid);
		rc = cob_locate(it, cob_fid);
		++sa->sa_unit;
	} while (rc == -ENOENT ||
		 cob_fid->f_container == it->si_fdata);

	if (rc == 0)
		iter_phase_set(it, ITPH_CP_SETUP);

	return rc;
}

/**
 * Transitions the data iterator (m0_sns_cm::sc_it) to ITPH_FID_NEXT
 * in-order to find the first GOB and parity group that needs repair.
 */
M0_INTERNAL int iter_init(struct m0_sns_cm_iter *it)
{
	iter_phase_set(it, ITPH_FID_NEXT);
	return 0;
}

static int (*iter_action[])(struct m0_sns_cm_iter *it) = {
	[ITPH_INIT]            = iter_init,
	[ITPH_COB_NEXT]        = iter_cob_next,
	[ITPH_GROUP_NEXT]      = iter_group_next,
	[ITPH_GROUP_NEXT_WAIT] = iter_group_next_wait,
	[ITPH_FID_NEXT]        = iter_fid_next,
	[ITPH_FID_NEXT_WAIT]   = iter_fid_next_wait,
	[ITPH_CP_SETUP]        = iter_cp_setup,
};

/**
 * Calculates next data object to be re-structured and accordingly populates
 * the given copy packet.
 */
M0_INTERNAL int m0_sns_cm_iter_next(struct m0_cm *cm, struct m0_cm_cp *cp)
{
	struct m0_sns_cm *scm;
	int                      rc;

	M0_PRE(cm != NULL && cp != NULL);

	scm = cm2sns(cm);
	scm->sc_it.si_cp = cp2snscp(cp);
	do {
		rc = iter_action[iter_phase(&scm->sc_it)](&scm->sc_it);
		M0_ASSERT(iter_invariant(&scm->sc_it));
	} while (rc == 0);

	return rc;
}

static const struct m0_sm_state_descr cm_iter_sd[ITPH_NR] = {
	[ITPH_INIT] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "iter init",
		.sd_allowed = M0_BITS(ITPH_FID_NEXT)
	},
	[ITPH_COB_NEXT] = {
		.sd_flags   = 0,
		.sd_name    = "COB next",
		.sd_allowed = M0_BITS(ITPH_GROUP_NEXT, ITPH_CP_SETUP, ITPH_FINI)
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
				      ITPH_FINI)
	},
	[ITPH_FID_NEXT_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "FID next wait",
		.sd_allowed = M0_BITS(ITPH_GROUP_NEXT)
	},
	[ITPH_CP_SETUP] = {
		.sd_flags   = 0,
		.sd_name    = "cp setup",
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

/**
 * Configures pdclust layout with default parameters, N = 1, K = 1 and
 * P = N + 2K. Eventually the layout for a particular file will be fetched as
 * part of the file attributes.
 * @note The default parameters and layout setup code are similar to that of
 * core/m0t1fs/linux_kernel/super.c used in m0t1fs client.
 * This also puts a temporary limitation on the client to mount m0t1fs with the
 * same default parameters.
 * @todo Fetch layout details dynamically.
 */
static int layout_setup(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm_pdclust_layout *spl;
	struct m0_layout_linear_attr     lattr;
	struct m0_pdclust_attr           plattr;
	struct m0_pdclust_layout        *pl;
	struct m0_dbenv                 *dbenv;
	uint64_t                         lid;
	int                              rc;

	dbenv = it->si_dbenv;
	rc = m0_layout_domain_init(&it->si_lay_dom, dbenv);
	if (rc != 0)
		return rc;
	rc = m0_layout_standard_types_register(&it->si_lay_dom);
	if (rc != 0) {
		m0_layout_domain_fini(&it->si_lay_dom);
		return rc;
	}
	spl = &it->si_pl;
	if (spl->spl_N == 0 || spl->spl_K == 0 || spl->spl_P == 0) {
		spl->spl_N = SNS_DEFAULT_NR_DATA_UNITS;
		spl->spl_K = SNS_DEFAULT_NR_PARITY_UNITS;
		spl->spl_P = SNS_DEFAULT_POOL_WIDTH;
	}
	lattr.lla_nr = spl->spl_P;
	lattr.lla_A  = 1;
	lattr.lla_B  = 1;
	rc = m0_linear_enum_build(&it->si_lay_dom, &lattr, &spl->spl_le);
	if (rc == 0) {
		lid                 = SNS_DEFAULT_LAYOUT_ID;
		plattr.pa_N         = spl->spl_N;
		plattr.pa_K         = spl->spl_K;
		plattr.pa_P         = spl->spl_P;
		plattr.pa_unit_size = m0_pagesize_get();
		m0_uint128_init(&plattr.pa_seed, "upjumpandpumpim,");
		rc = m0_pdclust_build(&it->si_lay_dom, lid, &plattr,
				      &spl->spl_le->lle_base, &spl->spl_base);
		if (rc != 0) {
			m0_layout_enum_fini(&spl->spl_le->lle_base);
			return rc;
		}

		pl = spl->spl_base;
		spl->spl_dpupg = m0_pdclust_N(pl) + m0_pdclust_K(pl);
		spl->spl_upg = m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
	}

	return rc;
}

static void layout_fini(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm_pdclust_layout *spl;

	M0_PRE(iter_invariant(it));

	spl = &it->si_pl;
	if (spl->spl_pi != NULL) {
		m0_layout_instance_fini(&spl->spl_pi->pi_base);
		spl->spl_pi = NULL;
	}
	m0_layout_put(m0_pdl_to_layout(spl->spl_base));
	m0_layout_standard_types_unregister(&it->si_lay_dom);
	m0_layout_domain_fini(&it->si_lay_dom);
}

M0_INTERNAL int m0_sns_cm_iter_init(struct m0_sns_cm_iter *it)
{
	struct m0_sns_cm     *scm = it2sns(it);
	struct m0_cm         *cm;
	int                   rc;
	struct m0_dbenv      *dbenv;
	struct m0_cob_domain *cdom;
	/*
	 * Pick the best possible fid to initialise the namespace iter.
	 * m0t1fs starts its fid space from {0,4}.
	 * XXX This should be changed to {0, 0} once multiple cob domains
	 * are added per service.
	 */
	struct m0_fid         gfid = {0, 4};

	M0_PRE(it != NULL);

	cm = &scm->sc_base;
	it->si_dbenv = cm->cm_service.rs_reqh->rh_dbenv;
        rc = m0_ios_cdom_get(cm->cm_service.rs_reqh, &it->si_cob_dom, 0);
        if (rc != 0)
                return rc;
	rc = layout_setup(it);
	if (rc != 0)
		return rc;

	m0_sm_init(&it->si_sm, &cm_iter_sm_conf, ITPH_INIT,
		   &cm->cm_sm_group, &cm->cm_service.rs_addb_ctx);
	m0_sns_cm_iter_bob_init(it);
	rc = m0_cob_ns_iter_init(&it->ri_cns_it, &gfid, it->si_dbenv, it->si_cob_dom);

	return rc;
}

M0_INTERNAL void m0_sns_cm_iter_fini(struct m0_sns_cm_iter *it)
{
	M0_PRE(it != NULL);

	m0_cob_ns_iter_fini(it->si_cns_it);
	layout_fini(it);
	iter_phase_set(it, ITPH_FINI);
	m0_sm_fini(&it->si_sm);
	m0_sns_cm_iter_bob_fini(it);
}

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
