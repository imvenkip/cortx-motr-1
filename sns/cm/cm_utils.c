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
 * Original creation date: 03/08/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/misc.h"
#include "lib/trace.h"
#include "lib/finject.h"

#include "cob/cob.h"
#include "mero/setup.h"
#include "ioservice/io_service.h"

#include "pool/pool.h"
#include "sns/parity_repair.h"
#include "sns/cm/ag.h"
#include "sns/cm/iter.h"
#include "sns/cm/cm_utils.h"
#include "sns/cm/file.h"

/**
   @addtogroup SNSCM

   @{
 */

/* Start of UT specific code. */
enum {
	SNS_DEFAULT_FILE_SIZE = 1 << 17,
	SNS_DEFAULT_N = 3,
	SNS_DEFAULT_K = 1,
	SNS_DEFAULT_P = 10,
	SNS_DEFAULT_UNIT_SIZE = 4096,
	SNS_DEFAULT_LAYOUT_ID = 0xAC1DF00D
};

M0_INTERNAL int
m0_sns_cm_ut_file_size_layout(struct m0_sns_cm_file_ctx *fctx)
{
	struct m0_sns_cm                    *scm = fctx->sf_scm;
	struct m0_layout_linear_attr         lattr;
	struct m0_pdclust_attr               plattr;
	struct m0_pdclust_layout            *pl;
	struct m0_layout_linear_enum        *le;
	struct m0_dbenv                     *dbenv;
	struct m0_reqh                      *reqh;
	uint64_t                             lid;
	int                                  rc = 0;

	reqh = scm->sc_base.cm_service.rs_reqh;
	dbenv = reqh->rh_dbenv;
	fctx->sf_layout = m0_layout_find(&reqh->rh_ldom, SNS_DEFAULT_LAYOUT_ID);
	if (fctx->sf_layout != NULL)
		goto out;
	lattr.lla_nr = SNS_DEFAULT_P;
	lattr.lla_A  = 1;
	lattr.lla_B  = 1;
	rc = m0_linear_enum_build(&reqh->rh_ldom, &lattr, &le);
	if (rc == 0) {
		lid                 = SNS_DEFAULT_LAYOUT_ID;
		plattr.pa_N         = SNS_DEFAULT_N;
		plattr.pa_K         = SNS_DEFAULT_K;
		plattr.pa_P         = SNS_DEFAULT_P;
		plattr.pa_unit_size = SNS_DEFAULT_UNIT_SIZE;
		m0_uint128_init(&plattr.pa_seed, "upjumpandpumpim,");
		rc = m0_pdclust_build(&reqh->rh_ldom, lid, &plattr,
				&le->lle_base, &pl);
		if (rc != 0) {
			m0_layout_enum_fini(&le->lle_base);
			return rc;
		}
	}
	fctx->sf_layout = m0_pdl_to_layout(pl);
out:
	fctx->sf_attr.ca_size = SNS_DEFAULT_FILE_SIZE;

	return rc;
}

/* End of UT specific code. */

M0_INTERNAL void m0_sns_cm_unit2cobfid(struct m0_pdclust_layout *pl,
				       struct m0_pdclust_instance *pi,
				       const struct m0_pdclust_src_addr *sa,
				       struct m0_pdclust_tgt_addr *ta,
				       const struct m0_fid *gfid,
				       struct m0_fid *cfid_out)
{
	struct m0_layout_enum      *le;

	m0_pdclust_instance_map(pi, sa, ta);
	le = m0_layout_to_enum(m0_pdl_to_layout(pl));
	m0_layout_enum_get(le, ta->ta_obj, gfid, cfid_out);
}

M0_INTERNAL uint64_t m0_sns_cm_ag_unit2cobindex(struct m0_sns_cm_ag *sag,
						uint64_t unit,
						struct m0_pdclust_layout *pl,
						struct m0_pdclust_instance *pi)
{
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_fid               gobfid;

	agid2fid(&sag->sag_base.cag_id, &gobfid);
	sa.sa_group = agid2group(&sag->sag_base.cag_id);
	sa.sa_unit  = unit;
	m0_pdclust_instance_map(pi, &sa, &ta);
	return ta.ta_frame * m0_pdclust_unit_size(pl);
}

M0_INTERNAL uint64_t m0_sns_cm_nr_groups(struct m0_pdclust_layout *pl,
					 uint64_t fsize)
{
	uint64_t nr_data_bytes_per_group;

	nr_data_bytes_per_group = m0_pdclust_N(pl) *
				  m0_pdclust_unit_size(pl);
	return fsize % nr_data_bytes_per_group ?
	       fsize / nr_data_bytes_per_group + 1 :
	       fsize / nr_data_bytes_per_group;
}

M0_INTERNAL int m0_sns_cm_cob_locate(struct m0_cob_domain *cdom,
				     const struct m0_fid *cob_fid)
{
	struct m0_cob        *cob;
	struct m0_cob_oikey   oikey;
	int                   rc;

	M0_ENTRY("dom=%p cob="FID_F, cdom, FID_P(cob_fid));

	m0_cob_oikey_make(&oikey, cob_fid, 0);
	rc = m0_cob_locate(cdom, &oikey, M0_CA_NSKEY_FREE, &cob);
	if (rc == 0) {
		M0_ASSERT(m0_fid_eq(cob_fid, m0_cob_fid(cob)));
		m0_cob_put(cob);
	}

	return M0_RC(rc);
}

M0_INTERNAL uint64_t m0_sns_cm_ag_nr_local_units(struct m0_sns_cm *scm,
						 const struct m0_fid *fid,
						 struct m0_pdclust_layout *pl,
						 struct m0_pdclust_instance *pi,
						 uint64_t group)
{
	struct m0_sns_cm_iter         *it  = &scm->sc_it;
	struct m0_pdclust_src_addr     sa;
	struct m0_pdclust_tgt_addr     ta;
	struct m0_fid                  cobfid;
	uint64_t                       nrlu = 0;
	int                            rc;
	int                            i;
	int                            start;
	int                            end;
	M0_ENTRY();

	M0_PRE(scm != NULL && fid != NULL && pl != NULL && pi != NULL);

	start = m0_sns_cm_ag_unit_start(scm, pl);
	end = m0_sns_cm_ag_unit_end(scm, pl);
	sa.sa_group = group;
	for (i = start; i < end; ++i) {
		sa.sa_unit = i;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, fid, &cobfid);
		rc = m0_sns_cm_cob_locate(it->si_cob_dom, &cobfid);
		if (rc == 0 && !m0_sns_cm_is_cob_failed(scm, &cobfid) &&
		    !m0_sns_cm_unit_is_spare(scm, pl, pi, fid, group, i))
			M0_CNT_INC(nrlu);
	}
	M0_LEAVE("number of local units = %lu", nrlu);
	return nrlu;
}

M0_INTERNAL
uint64_t m0_sns_cm_ag_nr_global_units(const struct m0_sns_cm_ag *sag,
		struct m0_pdclust_layout *pl)
{
	return m0_pdclust_N(pl) + m0_pdclust_K(pl);
}

M0_INTERNAL bool m0_sns_cm_is_cob_failed(const struct m0_sns_cm *scm,
					 const struct m0_fid *cob_fid)
{
	enum m0_pool_nd_state state_out = 0;
	m0_poolmach_device_state(scm->sc_base.cm_pm, cob_fid->f_container,
				 &state_out);
	return M0_IN(state_out, (M0_PNDS_FAILED, M0_PNDS_SNS_REPAIRING,
				 M0_PNDS_SNS_REPAIRED, M0_PNDS_SNS_REBALANCING));
}

M0_INTERNAL bool m0_sns_cm_is_cob_repaired(const struct m0_sns_cm *scm,
					   const struct m0_fid *cob_fid)
{
	enum m0_pool_nd_state state_out = 0;
	m0_poolmach_device_state(scm->sc_base.cm_pm, cob_fid->f_container,
				 &state_out);
	return state_out == M0_PNDS_SNS_REPAIRED;
}

M0_INTERNAL bool m0_sns_cm_unit_is_spare(const struct m0_sns_cm *scm,
					 struct m0_pdclust_layout *pl,
					 struct m0_pdclust_instance *pi,
					 const struct m0_fid *fid,
					 uint64_t group_nr, uint64_t spare_nr)
{
	uint64_t                    data_unit_id_out;
	struct m0_fid               cobfid;
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	enum m0_pool_nd_state       state_out = 0;
	bool                        result = true;
	int                         rc;

	if (m0_pdclust_unit_classify(pl, spare_nr) == M0_PUT_SPARE) {
		/*
		 * Firstly, check if the device corresponding to the given spare
		 * unit is already repaired. If yes then the spare unit is empty
		 * or the data is already moved. So we need not repair the spare
		 * unit.
		 */
		M0_SET0(&sa);
		M0_SET0(&ta);
		sa.sa_unit = spare_nr;
		sa.sa_group = group_nr;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, fid, &cobfid);
		rc = m0_poolmach_device_state(scm->sc_base.cm_pm,
			cobfid.f_container, &state_out);
		M0_ASSERT(rc == 0);
		if (state_out == M0_PNDS_SNS_REPAIRED)
			goto out;

		/*
		 * Failed spare unit may contain data of previously failed data
		 * unit from the parity group. Reverse map the spare unit to the
		 * repaired data/parity unit from the parity group.
		 * If we fail to map the spare unit to any of the previously
		 * failed data unit, means the spare is empty.
		 */
		rc = m0_sns_repair_data_map(scm->sc_base.cm_pm, pl, pi,
					    group_nr, spare_nr,
					    &data_unit_id_out);
		if (rc == -ENOENT)
			goto out;

		M0_SET0(&sa);
		M0_SET0(&ta);

		sa.sa_unit = data_unit_id_out;
		sa.sa_group = group_nr;

		/*
		 * The mechanism to reverse map the spare unit to any of the
		 * previously failed data unit is generic and based to device
		 * failure information from the pool machine.
		 * Thus if the device hosting the reverse mapped data unit for the
		 * given spare is in M0_PNDS_SNS_REPAIRED state, means the given
		 * spare unit contains data and needs to be repaired, else it is
		 * empty.
		 */
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, fid, &cobfid);
		if (m0_poolmach_device_is_in_spare_usage_array(
					scm->sc_base.cm_pm,
					cobfid.f_container)) {
			rc = m0_poolmach_device_state(scm->sc_base.cm_pm,
				cobfid.f_container, &state_out);
			M0_ASSERT(rc == 0);
			if (!M0_IN(state_out, (M0_PNDS_SNS_REPAIRED,
					       M0_PNDS_SNS_REBALANCING)))
				goto out;
		}
	}
	result = false;
out:
	return result;
}

M0_INTERNAL uint64_t m0_sns_cm_ag_spare_unit_nr(const struct m0_pdclust_layout *pl,
						uint64_t fidx)
{
        return m0_pdclust_N(pl) + m0_pdclust_K(pl) + fidx;
}

M0_INTERNAL uint64_t m0_sns_cm_ag_unit_start(const struct m0_sns_cm *scm,
					     const struct m0_pdclust_layout *pl)
{
        return scm->sc_helpers->sch_ag_unit_start(pl);
}

M0_INTERNAL uint64_t m0_sns_cm_ag_unit_end(const struct m0_sns_cm *scm,
					   const struct m0_pdclust_layout *pl)
{
	return scm->sc_helpers->sch_ag_unit_end(pl);
}

M0_INTERNAL int m0_sns_cm_ag_tgt_unit2cob(struct m0_sns_cm_ag *sag,
					  uint64_t tgt_unit,
					  struct m0_pdclust_layout *pl,
					  struct m0_pdclust_instance *pi,
					  struct m0_fid *cobfid)
{
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_fid               gobfid;

        agid2fid(&sag->sag_base.cag_id, &gobfid);
	sa.sa_group = agid2group(&sag->sag_base.cag_id);
	sa.sa_unit  = tgt_unit;
	m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, &gobfid, cobfid);

	return 0;
}

M0_INTERNAL const char *m0_sns_cm_tgt_ep(struct m0_cm *cm,
					 struct m0_fid *cfid)
{
	struct m0_reqh              *reqh   = cm->cm_service.rs_reqh;
	struct m0_mero              *mero   = m0_cs_ctx_get(reqh);
	struct cs_endpoint_and_xprt *ex;
	uint32_t                     nr_ios =
					cs_eps_tlist_length(&mero->cc_ios_eps);
	uint32_t                     pw     = mero->cc_pool_width;
	uint32_t                     nr_cnt_per_ios;
	uint64_t                     nr_cnts;

	nr_cnt_per_ios = pw / nr_ios;
	if (pw % nr_ios != 0)
		++nr_cnt_per_ios;
	nr_cnts = nr_cnt_per_ios;
	m0_tl_for(cs_eps, &mero->cc_ios_eps, ex) {
		if (cfid->f_container > nr_cnts) {
			nr_cnts += nr_cnt_per_ios;
			continue;
		}
		M0_LOG(M0_DEBUG, "Target endpoint for: "FID_F" %s",
		       FID_P(cfid), ex->ex_endpoint);
		return ex->ex_endpoint;
	} m0_tl_endfor;

	return NULL;
}

M0_INTERNAL size_t m0_sns_cm_ag_failures_nr(const struct m0_sns_cm *scm,
					    const struct m0_fid *gfid,
					    struct m0_pdclust_layout *pl,
					    struct m0_pdclust_instance *pi,
					    uint64_t group,
					    struct m0_bitmap *fmap_out)
{
	struct m0_pdclust_src_addr sa;
	struct m0_pdclust_tgt_addr ta;
	struct m0_fid              cobfid;
	uint64_t                   upg;
	uint64_t                   unit;
	size_t                     group_failures = 0;
	M0_ENTRY();

	M0_PRE(scm != NULL && pl != NULL);

	upg = m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
	sa.sa_group = group;
	for (unit = 0; unit < upg; ++unit) {
		if (m0_sns_cm_unit_is_spare(scm, pl, pi, gfid, group, unit))
			continue;
		sa.sa_unit = unit;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, gfid, &cobfid);
		if (m0_sns_cm_is_cob_failed(scm, &cobfid)) {
			M0_CNT_INC(group_failures);
			if (fmap_out != NULL) {
				M0_ASSERT(fmap_out->b_nr == upg);
				m0_bitmap_set(fmap_out, unit, true);
			}
		}
	}

	M0_LEAVE("number of faulure groups = %lu", group_failures);
	return group_failures;
}

M0_INTERNAL bool m0_sns_cm_ag_is_relevant(struct m0_sns_cm *scm,
                                          struct m0_pdclust_layout *pl,
					  struct m0_pdclust_instance *pi,
                                          const struct m0_cm_ag_id *id)
{
        struct m0_fid               fid;
        size_t                      group_failures;
        uint64_t                    group;
        bool                        result = false;

	M0_ENTRY();

        agid2fid(id,  &fid);
	group = id->ai_lo.u_lo;
	/* Firstly check if this group has any failed units. */
	group_failures = m0_sns_cm_ag_failures_nr(scm, &fid, pl,
						  pi, group, NULL);
	if (group_failures > 0 )
		result = scm->sc_helpers->sch_ag_is_relevant(scm, &fid,
							     group, pl,
							     pi);
        return M0_RC(result);
}

M0_INTERNAL uint64_t
m0_sns_cm_ag_max_incoming_units(const struct m0_sns_cm *scm,
				const struct m0_cm_ag_id *id,
				struct m0_pdclust_layout *pl,
				struct m0_pdclust_instance *pi)
{
	M0_PRE(m0_cm_is_locked(&scm->sc_base));

	return scm->sc_helpers->sch_ag_max_incoming_units(scm, id, pl, pi);
}

M0_INTERNAL bool m0_sns_cm_fid_is_valid(const struct m0_fid *fid)
{
        return fid->f_container >= 0 && fid->f_key >=
               M0_MDSERVICE_START_FID.f_key;
}

#undef M0_TRACE_SUBSYSTEM

/** @} endgroup SNSCM */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
