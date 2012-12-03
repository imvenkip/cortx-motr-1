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

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_SNSREPAIR
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
#include "mdstore/mdstore.h"
#include "sns/repair/cm.h"
#include "sns/repair/cp.h"
#include "sns/repair/ag.h"

/**
  @addtogroup SNSRepairCM

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
	 * TODO: SNS_FILE_SIZE is temporary hard coded file size used for
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
static const struct c2_fid default_single_file_fid = {
	.f_container = 0,
	.f_key = 4
};

static const struct c2_bob_type iter_bob = {
	.bt_name = "sns repair data iterator",
	.bt_magix_offset = C2_MAGIX_OFFSET(struct c2_sns_repair_iter, ri_magix),
	.bt_magix = SNS_REPAIR_ITER_MAGIX,
	.bt_check = NULL
};

C2_BOB_DEFINE(static, &iter_bob, c2_sns_repair_iter);

enum cm_data_iter_phase {
	/**
	 * Iterator is in this phase when c2_cm:cm_ops::cmo_data_next() is first
	 * invoked as part of c2_cm_start() and c2_cm_cp_pump_start(), from
	 * c2_cm_data_next(). This starts the sns repair data iterator and sets
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
	 * @see struct c2_sns_repair_cp
	 */
	ITPH_CP_SETUP,
	/**
	 * Iterator is finalised after the sns repair operation is complete.
	 * This is done as part of c2_cm_stop().
	 */
	ITPH_FINI,
	ITPH_NR
};

enum {
	IT_WAIT = C2_FSO_WAIT
};

/**
 * Returns current iterator phase.
 */
static enum cm_data_iter_phase iter_phase(const struct c2_sns_repair_iter *it)
{
	return it->ri_sm.sm_state;
}

/**
 * Sets iterator phase.
 */
static void iter_phase_set(struct c2_sns_repair_iter *it, int phase)
{
	c2_sm_state_set(&it->ri_sm, phase);
}

static bool
iter_layout_invariant(enum cm_data_iter_phase phase,
                      const struct c2_sns_repair_pdclust_layout *rpl)
{
	return ergo(C2_IN(phase, (ITPH_COB_NEXT, ITPH_GROUP_NEXT,
				 ITPH_GROUP_NEXT_WAIT, ITPH_CP_SETUP)),
		   rpl->rpl_base != NULL && rpl->rpl_le != NULL &&
		   rpl->rpl_pi != NULL && rpl->rpl_N != 0 && rpl->rpl_K != 0 &&
		   rpl->rpl_P != 0 && rpl->rpl_upg != 0 &&
		   rpl->rpl_P >= (rpl->rpl_N + 2 * rpl->rpl_K) &&
		   rpl->rpl_dpupg != 0 && c2_fid_is_set(&rpl->rpl_gob_fid)) &&
	       ergo(C2_IN(phase, (ITPH_CP_SETUP)), rpl->rpl_groups_nr != 0 &&
		   c2_fid_is_set(&rpl->rpl_cob_fid) &&
		   rpl->rpl_sa.sa_group <= rpl->rpl_groups_nr &&
		   rpl->rpl_sa.sa_unit <= rpl->rpl_upg &&
		   rpl->rpl_ta.ta_obj <= rpl->rpl_P);
}

static bool iter_invariant(const struct c2_sns_repair_iter *it)
{
	enum cm_data_iter_phase phase = iter_phase(it);

	return it != NULL && c2_sns_repair_iter_bob_check(it) &&
	       it->ri_cp != NULL && iter_layout_invariant(phase, &it->ri_pl);
}

/**
 * Fetches total size of a file corresponding to the given GOB fid
 * (c2_sns_repair_cm::ri_pl::rpl_gob_fid). This is used to calculate
 * total number of parity groups per GOB.
 * Returns 0 * @todo Currently the file size is hard coded, but eventually it would be
 * retrieved as part of file attributes.
 * @note Fetching file attributes may block.
 * @retval 0 on success, IT_WAIT for blocking operation
 */
static ssize_t file_size(struct c2_sns_repair_cm *rcm)
{
	C2_PRE(rcm != NULL);

	rcm->rc_it.ri_pl.rpl_fsize = rcm->rc_file_size;
	return 0;
}

/**
 * Fetches file layout for rcm->rc_it.ri_pl.rpl_gob_fid.
 * @todo To fetch file layout as part of file attributes.
 * @note This may block.
 * @retval 0 on success, IT_WAIT for blocking operation
 */
static int cm_layout_fetch(struct c2_sns_repair_cm *rcm)
{
	C2_PRE(rcm != NULL);

	return 0;
}

/*
 * Searches for given cob_fid in the local cob domain.
 */
static int cob_locate(const struct c2_sns_repair_cm *rcm,
		      const struct c2_fid *cob_fid)
{
	struct c2_cob        *cob;
	struct c2_cob_oikey   oikey;
	struct c2_db_tx       tx;
	struct c2_dbenv      *dbenv;
	struct c2_cob_domain *cdom;
	int                   rc;

	dbenv = rcm->rc_base.cm_service.rs_reqh->rh_dbenv;
	cdom = &rcm->rc_base.cm_service.rs_reqh->rh_mdstore->md_dom;
	C2_ASSERT(cdom != NULL);

	rc = c2_db_tx_init(&tx, dbenv, 0);
	if (rc != 0)
		return rc;
	c2_cob_oikey_make(&oikey, cob_fid, 0);
	rc = c2_cob_locate(cdom, &oikey, 0, &cob, &tx);
	if (rc == 0) {
		C2_ASSERT(c2_fid_eq(cob_fid, cob->co_fid));
		c2_db_tx_commit(&tx);
	} else
		c2_db_tx_abort(&tx);

	return rc;
}

static bool unit_is_spare(const struct c2_pdclust_layout *pl, int unit)
{
	return c2_pdclust_unit_classify(pl, unit) == C2_PUT_SPARE;
}

/*
 * Returns cob fid for the sa->sa_unit.
 * @see c2_pdclust_instance_map
 */
static void __unit_to_cobfid(struct c2_pdclust_layout *pl,
			     struct c2_pdclust_instance *pi,
			     const struct c2_pdclust_src_addr *sa,
			     struct c2_pdclust_tgt_addr *ta,
			     const struct c2_fid *gfid, struct c2_fid *cfid_out)
{
	struct c2_layout_enum *le;

	c2_pdclust_instance_map(pi, sa, ta);
	le = c2_layout_to_enum(c2_pdl_to_layout(pl));
	c2_layout_enum_get(le, ta->ta_obj, gfid, cfid_out);
}

C2_INTERNAL uint64_t nr_local_units(struct c2_sns_repair_cm *rcm,
				    const struct c2_fid *fid, uint64_t group)
{
	struct c2_sns_repair_pdclust_layout *rpl = &rcm->rc_it.ri_pl;
	struct c2_pdclust_src_addr           sa;
	struct c2_pdclust_tgt_addr           ta;
	struct c2_fid                        cobfid;
	uint64_t                             nrlu = 0;
	int                                  rc;
	int                                  i;

	C2_PRE(iter_invariant(&rcm->rc_it));
	C2_PRE(c2_fid_eq(fid, &rpl->rpl_gob_fid));

	sa.sa_group = group;
	for (i = 0; i < rpl->rpl_dpupg; ++i) {
		sa.sa_unit = i;
		C2_SET0(&ta);
		C2_SET0(&cobfid);
		__unit_to_cobfid(rpl->rpl_base, rpl->rpl_pi, &sa, &ta, fid,
				 &cobfid);
		rc = cob_locate(rcm, &cobfid);
		if (rc == 0 && cobfid.f_container != rcm->rc_fdata)
			++nrlu;
	}

	return nrlu;
}

/**
 * Returns index of spare unit in the parity group.
 */
static uint64_t __spare_unit_nr(const struct c2_sns_repair_pdclust_layout *rpl,
				uint64_t group)
{
	return rpl->rpl_N + rpl->rpl_K + 1 - 1;
}

C2_INTERNAL void spare_unit_to_cob(struct c2_sns_repair_ag *rag)
{
	struct c2_sns_repair_cm             *rcm = cm2sns(rag->sag_base.cag_cm);
	struct c2_sns_repair_pdclust_layout *rpl = &rcm->rc_it.ri_pl;
	struct c2_pdclust_src_addr           sa;
	struct c2_pdclust_tgt_addr           ta;
	struct c2_fid                        gobfid;
	struct c2_fid                        cobfid;


	C2_SET0(&ta);
	C2_SET0(&cobfid);
	agid2fid(&rag->sag_base, &gobfid);
	C2_ASSERT(c2_fid_eq(&gobfid, &rpl->rpl_gob_fid));
	sa.sa_group = agid2group(&rag->sag_base);
	sa.sa_unit = __spare_unit_nr(rpl, sa.sa_group);
	__unit_to_cobfid(rpl->rpl_base, rpl->rpl_pi, &sa, &ta, &gobfid,
			 &cobfid);
	rag->sag_spare_cobfid = cobfid;
	rag->sag_spare_cob_index = ta.ta_frame *
				   c2_pdclust_unit_size(rpl->rpl_base);
}

/**
 * Calculates COB fid for c2_sns_repair_iter::ri_pl::ri_sa.
 * Saves calculated struct c2_pdclust_tgt_addr in
 * c2_sns_repair_pdclust_layout::rpl_ta.
 */
static void unit_to_cobfid(struct c2_sns_repair_pdclust_layout *rpl,
			   struct c2_fid *cob_fid_out)
{
	struct c2_pdclust_instance  *pi;
	struct c2_pdclust_layout    *pl;
	struct c2_pdclust_src_addr  *sa;
	struct c2_pdclust_tgt_addr  *ta;
	struct c2_fid               *fid;

	fid = &rpl->rpl_gob_fid;
	pi = rpl->rpl_pi;
	pl = rpl->rpl_base;
	sa = &rpl->rpl_sa;
	ta = &rpl->rpl_ta;
	rpl->rpl_cob_is_spare_unit = unit_is_spare(pl, sa->sa_unit);
	__unit_to_cobfid(pl, pi, sa, ta, fid, cob_fid_out);
}

/**
 * Builds layout instance for new GOB fid calculated in ITPH_FID_NEXT phase.
 * @see iter_fid_next()
 */
static int fid_layout_build(struct c2_sns_repair_cm *rcm)
{
	struct c2_sns_repair_pdclust_layout *rpl;
	struct c2_pdclust_layout            *pl;
	struct c2_layout_instance           *li;
	struct c2_fid                       *fid;
	int                                  rc;

	rpl = &rcm->rc_it.ri_pl;
	fid = &rpl->rpl_gob_fid;
	pl  = rpl->rpl_base;
        /* Destroy previous pdclust instance */
	if (rpl->rpl_pi != NULL)
		c2_layout_instance_fini(&rpl->rpl_pi->pi_base);
	rc = c2_layout_instance_build(&pl->pl_base.sl_base, fid, &li);
	if (rc == 0) {
		rpl->rpl_pi = c2_layout_instance_to_pdi(li);
		rpl->rpl_sa.sa_group = 0;
		rpl->rpl_sa.sa_unit = 0;
		iter_phase_set(&rcm->rc_it, ITPH_GROUP_NEXT);
	}

	return rc;
}

static int iter_fid_next_wait(struct c2_sns_repair_cm *rcm)
{
	return fid_layout_build(rcm);
}

/* Uses name space iterator. */
static int __fid_next(struct c2_sns_repair_cm *rcm, struct c2_fid *fid_next)
{
	int             rc;
	struct c2_db_tx tx;

	rc = c2_db_tx_init(&tx, rcm->rc_base.cm_service.rs_reqh->rh_dbenv, 0);
	if (rc != 0)
		return rc;

	rc = c2_cob_ns_iter_next(&rcm->rc_it.ri_cns_it, &tx, fid_next);
        if (rc == 0)
                c2_db_tx_commit(&tx);
        else
                c2_db_tx_abort(&tx);

	return rc;
}

/**
 * Fetches next GOB fid.
 * @note Presently uses a hard coded GOB fid for single file repair.
 * @todo Use name space iterator to fetch next GOB fid.
 */
static int iter_fid_next(struct c2_sns_repair_cm *rcm)
{
	struct c2_fid              fid_next;
	int                        rc;

	/* Get current GOB fid saved in the iterator. */
	rc = __fid_next(rcm, &fid_next);
	if (rc == -ENOENT)
		return -ENODATA;
	if (rc == 0) {
		/* Save next GOB fid in the iterator. */
		rcm->rc_it.ri_pl.rpl_gob_fid = fid_next;
		rc = cm_layout_fetch(rcm);
		if (rc == IT_WAIT) {
			iter_phase_set(&rcm->rc_it, ITPH_FID_NEXT_WAIT);
			return rc;
		}
		rc = fid_layout_build(rcm);
	}

	return rc;
}

static uint64_t nr_groups(struct c2_sns_repair_pdclust_layout *rpl)
{
	uint64_t nr_data_bytes_per_group;

	nr_data_bytes_per_group =  c2_pdclust_N(rpl->rpl_base) *
				   c2_pdclust_unit_size(rpl->rpl_base);
	return rpl->rpl_fsize % nr_data_bytes_per_group ?
	       rpl->rpl_fsize / nr_data_bytes_per_group + 1 :
	       rpl->rpl_fsize / nr_data_bytes_per_group;
}

/**
 * Finds parity group having units belonging to the failed container.
 * This iterates through each parity group of the file, and its units.
 * A COB id is calculated for each unit and checked if ti belongs to the
 * failed container, if yes then the group is selected for processing.
 * This is invoked from ITPH_GROUP_NEXT and ITPH_GROUP_NEXT_WAIT phase.
 */
static int __group_next(struct c2_sns_repair_cm *rcm)
{
	struct c2_sns_repair_pdclust_layout *rpl;
	struct c2_pdclust_src_addr          *sa;
	struct c2_fid                        cob_fid;
	uint64_t                             groups_nr;
	uint64_t                             group;
	uint64_t                             unit;

	rpl = &rcm->rc_it.ri_pl;
	rpl->rpl_groups_nr = groups_nr = nr_groups(rpl);
	sa = &rpl->rpl_sa;
	for (group = sa->sa_group; group < groups_nr; ++group) {
		for (unit = 0; unit < rpl->rpl_dpupg; ++unit) {
			rpl->rpl_sa.sa_unit = unit;
			rpl->rpl_sa.sa_group = group;
			unit_to_cobfid(rpl, &cob_fid);
			if (cob_fid.f_container == rcm->rc_fdata) {
				rpl->rpl_sa.sa_unit = 0;
				iter_phase_set(&rcm->rc_it, ITPH_COB_NEXT);
				goto out;
			}
		}
	}

	iter_phase_set(&rcm->rc_it, ITPH_FID_NEXT);
out:
	return 0;
}

static int iter_group_next_wait(struct c2_sns_repair_cm *rcm)
{
	return __group_next(rcm);
}

/**
 * Finds the next parity group to process.
 * @note This operation may block while fetching the file size, as part of file
 * attributes.
 */
static int iter_group_next(struct c2_sns_repair_cm *rcm)
{
	int rc;

	/* File size may have changed, fetch new file size. */
	rc = file_size(rcm);
	if (rc == IT_WAIT) {
		iter_phase_set(&rcm->rc_it, ITPH_GROUP_NEXT_WAIT);
		return rc;
	}
	rc = __group_next(rcm);

	return rc;
}

static int cm_buf_attach(struct c2_sns_repair_cm *rcm, struct c2_cm_cp *cp)
{
	struct c2_net_buffer    *buf;
	size_t                   colour;

	colour =  cp_home_loc_helper(cp) % rcm->rc_obp.nbp_colours_nr;
	buf = c2_sns_repair_buffer_get(&rcm->rc_obp, colour);
	if (buf == NULL)
		return -ENOBUFS;
	cp->c_data = &buf->nb_buffer;

	return 0;
}

static void agid_setup(const struct c2_fid *gob_fid, uint64_t group,
		       struct c2_cm_ag_id *agid)
{
	agid->ai_hi.u_hi = gob_fid->f_container;
	agid->ai_hi.u_lo = gob_fid->f_key;
	agid->ai_lo.u_hi = 0;
	agid->ai_lo.u_lo = group;
}

static void __cp_setup(struct c2_sns_repair_cp *rcp,
		       const struct c2_fid *cob_fid, uint64_t stob_offset,
		       struct c2_cm_aggr_group *ag)
{
	rcp->rc_sid.si_bits.u_hi = cob_fid->f_container;
	rcp->rc_sid.si_bits.u_lo = cob_fid->f_key;
	rcp->rc_index = stob_offset;
	rcp->rc_base.c_ag = ag;
}

/**
 * Configures the given copy packet with aggregation group and stob details.
 */
static int iter_cp_setup(struct c2_sns_repair_cm *rcm)
{
	struct c2_cm_ag_id                   agid;
	struct c2_sns_repair_ag             *rag;
	struct c2_sns_repair_pdclust_layout *rpl;
	struct c2_sns_repair_cp             *rcp;
	uint64_t                             stob_offset;
	int                                  rc = 0;

	rpl = &rcm->rc_it.ri_pl;
	agid_setup(&rpl->rpl_gob_fid, rpl->rpl_sa.sa_group, &agid);
	rag = c2_sns_repair_ag_find(rcm, &agid);

	if (rag == NULL)
		return -EINVAL;
	if (!rpl->rpl_cob_is_spare_unit) {
		stob_offset = rpl->rpl_ta.ta_frame *
			      c2_pdclust_unit_size(rpl->rpl_base);
		rcp = rcm->rc_it.ri_cp;
		__cp_setup(rcp, &rpl->rpl_cob_fid, stob_offset, &rag->sag_base);
		rc = cm_buf_attach(rcm, &rcp->rc_base);
	}

	if (rc == 0) {
		iter_phase_set(&rcm->rc_it, ITPH_COB_NEXT);
		/*
		 * If this is spare unit we just locate an aggregation group and
		 * do not fill the given copy packet, but proceed to next data
		 * or parity unit.
		 */
		rc = rpl->rpl_cob_is_spare_unit ? 0 : C2_FSO_AGAIN;
	}

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
static int iter_cob_next(struct c2_sns_repair_cm *rcm)
{
	struct c2_sns_repair_pdclust_layout *rpl;
	struct c2_fid                       *cob_fid;
	struct c2_pdclust_src_addr          *sa;
	struct c2_pdclust_layout            *pl;
	uint32_t                             upg;
	int                                  rc = 0;

	rpl = &rcm->rc_it.ri_pl;
	pl = rpl->rpl_base;
	upg = rpl->rpl_upg;
	sa = &rpl->rpl_sa;

	cob_fid = &rpl->rpl_cob_fid;
	do {
		if (sa->sa_unit >= upg) {
			++rcm->rc_it.ri_pl.rpl_sa.sa_group;
			iter_phase_set(&rcm->rc_it, ITPH_GROUP_NEXT);
			return 0;
		}
		/*
		 * Calculate COB fid corresponding to the unit and advance
		 * rcm->rc_it.ri_src::sa_unit to next unit in the parity
		 * group. If this is the last unit in the parity group then
		 * proceed to next parity group in the GOB.
		 */
		unit_to_cobfid(rpl, cob_fid);
		rc = cob_locate(rcm, cob_fid);
		++sa->sa_unit;
	} while (rc == -ENOENT ||
		 cob_fid->f_container == rcm->rc_fdata);

	if (rc == 0)
		iter_phase_set(&rcm->rc_it, ITPH_CP_SETUP);

	return rc;
}

/**
 * Transitions the data iterator (c2_sns_repair_cm::rc_it) to ITPH_FID_NEXT
 * in-order to find the first GOB and parity group that needs repair.
 */
C2_INTERNAL int iter_init(struct c2_sns_repair_cm *rcm)
{
	iter_phase_set(&rcm->rc_it, ITPH_FID_NEXT);
	return 0;
}

static int (*iter_action[])(struct c2_sns_repair_cm *rcm) = {
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
C2_INTERNAL int c2_sns_repair_iter_next(struct c2_cm *cm, struct c2_cm_cp *cp)
{
	struct c2_sns_repair_cm *rcm;
	int                      rc;

	C2_PRE(cm != NULL && cp != NULL);

	rcm = cm2sns(cm);
	rcm->rc_it.ri_cp = cp2snscp(cp);
	do {
		rc = iter_action[iter_phase(&rcm->rc_it)](rcm);
		C2_ASSERT(iter_invariant(&rcm->rc_it));
	} while (rc == 0);

	return rc;
}

static const struct c2_sm_state_descr cm_iter_sd[ITPH_NR] = {
	[ITPH_INIT] = {
		.sd_flags   = C2_SDF_INITIAL,
		.sd_name    = "iter init",
		.sd_allowed = C2_BITS(ITPH_FID_NEXT)
	},
	[ITPH_COB_NEXT] = {
		.sd_flags   = 0,
		.sd_name    = "COB next",
		.sd_allowed = C2_BITS(ITPH_GROUP_NEXT, ITPH_CP_SETUP, ITPH_FINI)
	},
	[ITPH_GROUP_NEXT] = {
		.sd_flags   = 0,
		.sd_name    = "group next",
		.sd_allowed = C2_BITS(ITPH_GROUP_NEXT_WAIT, ITPH_COB_NEXT,
				      ITPH_FID_NEXT)
	},
	[ITPH_GROUP_NEXT_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "group next wait",
		.sd_allowed = C2_BITS(ITPH_COB_NEXT, ITPH_FID_NEXT)
	},
	[ITPH_FID_NEXT] = {
		.sd_flags   = 0,
		.sd_name    = "FID next",
		.sd_allowed = C2_BITS(ITPH_FID_NEXT_WAIT, ITPH_GROUP_NEXT,
				      ITPH_FINI)
	},
	[ITPH_FID_NEXT_WAIT] = {
		.sd_flags   = 0,
		.sd_name    = "FID next wait",
		.sd_allowed = C2_BITS(ITPH_GROUP_NEXT)
	},
	[ITPH_CP_SETUP] = {
		.sd_flags   = 0,
		.sd_name    = "cp setup",
		.sd_allowed = C2_BITS(ITPH_COB_NEXT)
	},
	[ITPH_FINI] = {
		.sd_flags = C2_SDF_TERMINAL,
		.sd_name  = "cm iter fini",
		.sd_allowed = 0
	}
};

static const struct c2_sm_conf cm_iter_sm_conf = {
	.scf_name      = "sm: cm_iter_conf",
	.scf_nr_states = ARRAY_SIZE(cm_iter_sd),
	.scf_state     = cm_iter_sd
};

/**
 * Configures pdclust layout with default parameters, N = 1, K = 1 and
 * P = N + 2K. Eventually the layout for a particular file will be fetched as
 * part of the file attributes.
 * @note The default parameters and layout setup code are similar to that of
 * core/c2t1fs/linux_kernel/super.c used in c2t1fs client.
 * This also puts a temporary limitation on the client to mount c2t1fs with the
 * same default parameters.
 * @todo Fetch layout details dynamically.
 */
static int layout_setup(struct c2_sns_repair_cm *rcm)
{
	struct c2_sns_repair_pdclust_layout *rpl;
	struct c2_layout_linear_attr         lattr;
	struct c2_pdclust_attr               plattr;
	struct c2_pdclust_layout            *pl;
	struct c2_dbenv                     *dbenv;
	uint64_t                             lid;
	int                                  rc;

	dbenv = rcm->rc_base.cm_service.rs_reqh->rh_dbenv;
	rc = c2_layout_domain_init(&rcm->rc_lay_dom, dbenv);
	if (rc != 0)
		return rc;
	rc = c2_layout_standard_types_register(&rcm->rc_lay_dom);
	if (rc != 0) {
		c2_layout_domain_fini(&rcm->rc_lay_dom);
		return rc;
	}
	rpl = &rcm->rc_it.ri_pl;
	if (rpl->rpl_N == 0 || rpl->rpl_K == 0 || rpl->rpl_P == 0) {
		rpl->rpl_N = SNS_DEFAULT_NR_DATA_UNITS;
		rpl->rpl_K = SNS_DEFAULT_NR_PARITY_UNITS;
		rpl->rpl_P = SNS_DEFAULT_POOL_WIDTH;
	}
	lattr.lla_nr = rpl->rpl_P;
	lattr.lla_A  = 1;
	lattr.lla_B  = 1;
	rc = c2_linear_enum_build(&rcm->rc_lay_dom, &lattr, &rpl->rpl_le);
	if (rc == 0) {
		lid                 = SNS_DEFAULT_LAYOUT_ID;
		plattr.pa_N         = rpl->rpl_N;
		plattr.pa_K         = rpl->rpl_K;
		plattr.pa_P         = rpl->rpl_P;
		plattr.pa_unit_size = c2_pagesize_get();
		c2_uint128_init(&plattr.pa_seed, "upjumpandpumpim,");
		rc = c2_pdclust_build(&rcm->rc_lay_dom, lid, &plattr,
				      &rpl->rpl_le->lle_base, &rpl->rpl_base);
		if (rc != 0) {
			c2_layout_enum_fini(&rpl->rpl_le->lle_base);
			return rc;
		}

		pl = rpl->rpl_base;
		rpl->rpl_dpupg = c2_pdclust_N(pl) + c2_pdclust_K(pl);
		rpl->rpl_upg = c2_pdclust_N(pl) + 2 * c2_pdclust_K(pl);
	}

	return rc;
}

static void layout_fini(struct c2_sns_repair_cm *rcm)
{
	struct c2_sns_repair_pdclust_layout *rpl;

	C2_PRE(rcm != NULL);
	C2_PRE(iter_invariant(&rcm->rc_it));

	rpl = &rcm->rc_it.ri_pl;
	if (rpl->rpl_pi != NULL) {
		c2_layout_instance_fini(&rpl->rpl_pi->pi_base);
		rpl->rpl_pi = NULL;
	}
	c2_layout_put(c2_pdl_to_layout(rpl->rpl_base));
	c2_layout_standard_types_unregister(&rcm->rc_lay_dom);
	c2_layout_domain_fini(&rcm->rc_lay_dom);
}

C2_INTERNAL int c2_sns_repair_iter_init(struct c2_sns_repair_cm *rcm)
{
	struct c2_cm         *cm;
	int                   rc;
	struct c2_dbenv      *dbenv;
	struct c2_cob_domain *cdom;
	struct c2_fid         gfid = {1, 4};


	C2_PRE(rcm != NULL);

	cm = &rcm->rc_base;
	rc = layout_setup(rcm);
	if (rc != 0)
		return rc;

	c2_sm_init(&rcm->rc_it.ri_sm, &cm_iter_sm_conf,	ITPH_INIT,
		   &cm->cm_sm_group, &cm->cm_addb);
	c2_sns_repair_iter_bob_init(&rcm->rc_it);

	if (rcm->rc_file_size == 0)
		rcm->rc_file_size = SNS_FILE_SIZE_DEFAULT;

        dbenv = rcm->rc_base.cm_service.rs_reqh->rh_dbenv;
        cdom = &rcm->rc_base.cm_service.rs_reqh->rh_mdstore->md_dom;
	rc = c2_cob_ns_iter_init(&rcm->rc_it.ri_cns_it, &gfid, dbenv, cdom);

	return rc;
}

C2_INTERNAL void c2_sns_repair_iter_fini(struct c2_sns_repair_cm *rcm)
{
	C2_PRE(rcm != NULL);

	c2_cob_ns_iter_fini(&rcm->rc_it.ri_cns_it);
	layout_fini(rcm);
	iter_phase_set(&rcm->rc_it, ITPH_FINI);
	c2_sm_fini(&rcm->rc_it.ri_sm);
	c2_sns_repair_iter_bob_fini(&rcm->rc_it);
}

/** @} SNSRepairCM */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
