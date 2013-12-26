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

static int sns_cm_ut_file_size_layout_fetch(struct m0_cm *cm,
					    struct m0_fid *gfid,
					    struct m0_pdclust_layout **layout,
					    uint64_t *fsize)
{
	struct m0_layout_linear_attr         lattr;
	struct m0_pdclust_attr               plattr;
	struct m0_layout                    *l = NULL;
	struct m0_layout_linear_enum        *le;
	struct m0_dbenv                     *dbenv;
	struct m0_reqh                      *reqh;
	uint64_t                             lid;
	int                                  rc = 0;

	reqh = cm->cm_service.rs_reqh;
	dbenv = reqh->rh_dbenv;
	l = m0_layout_find(&reqh->rh_ldom, SNS_DEFAULT_LAYOUT_ID);
	if (l != NULL) {
		*layout = m0_layout_to_pdl(l);
		goto out;
	}
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
				&le->lle_base, layout);
		if (rc != 0) {
			m0_layout_enum_fini(&le->lle_base);
			return rc;
		}
	}
out:
	*fsize = SNS_DEFAULT_FILE_SIZE;

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
	struct m0_layout_enum *le;

	m0_pdclust_instance_map(pi, sa, ta);
	le = m0_layout_to_enum(m0_pdl_to_layout(pl));
	m0_layout_enum_get(le, ta->ta_obj, gfid, cfid_out);
}

M0_INTERNAL uint64_t m0_sns_cm_ag_unit2cobindex(struct m0_sns_cm_ag *sag,
						uint64_t unit,
						struct m0_pdclust_layout *pl)
{
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_fid               gobfid;
	struct m0_pdclust_instance *pi;
	int                         rc;

	agid2fid(&sag->sag_base.cag_id, &gobfid);
	rc = m0_sns_cm_fid_layout_instance(pl, &pi, &gobfid);
        if (rc != 0)
                return -ENOENT;
	sa.sa_group = agid2group(&sag->sag_base.cag_id);
	sa.sa_unit  = unit;
	m0_pdclust_instance_map(pi, &sa, &ta);
	m0_layout_instance_fini(&pi->pi_base);
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

	M0_RETURN(rc);
}

M0_INTERNAL uint64_t m0_sns_cm_ag_nr_local_units(struct m0_sns_cm *scm,
						 const struct m0_fid *fid,
						 struct m0_pdclust_layout *pl,
						 uint64_t group)
{
	struct m0_sns_cm_iter         *it  = &scm->sc_it;
	struct m0_pdclust_src_addr     sa;
	struct m0_pdclust_tgt_addr     ta;
	struct m0_pdclust_instance    *pi;
	struct m0_fid                  cobfid;
	uint64_t                       nrlu = 0;
	int                            rc;
	int                            i;
	int                            start;
	int                            end;

	M0_PRE(scm != NULL && fid != NULL && pl != NULL);

	rc = m0_sns_cm_fid_layout_instance(pl, &pi, fid);
	if (rc != 0)
		return 0;
	start = m0_sns_cm_ag_unit_start(scm, pl);
	end = m0_sns_cm_ag_unit_end(scm, pl);
	sa.sa_group = group;
	for (i = start; i < end; ++i) {
		sa.sa_unit = i;
		M0_SET0(&ta);
		M0_SET0(&cobfid);
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, fid, &cobfid);
		rc = m0_sns_cm_cob_locate(it->si_cob_dom, &cobfid);
		if (rc == 0 && !m0_sns_cm_is_cob_failed(scm, &cobfid) &&
		    !m0_sns_cm_unit_is_spare(scm, pl, fid, group, i))
			M0_CNT_INC(nrlu);
	}
	m0_layout_instance_fini(&pi->pi_base);

	return nrlu;
}

M0_INTERNAL
uint64_t m0_sns_cm_ag_nr_global_units(const struct m0_sns_cm_ag *sag,
		struct m0_pdclust_layout *pl)
{
	return m0_pdclust_N(pl) + m0_pdclust_K(pl);
}

M0_INTERNAL int m0_sns_cm_fid_layout_instance(struct m0_pdclust_layout *pl,
					      struct m0_pdclust_instance **pi,
					      const struct m0_fid *fid)
{
	struct m0_layout_instance *li;
	int                        rc;

	rc = m0_layout_instance_build(&pl->pl_base.sl_base, fid, &li);
	if (rc == 0)
		*pi = m0_layout_instance_to_pdi(li);

	return rc;
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
					 const struct m0_fid *fid,
					 uint64_t group_number,
					 uint64_t spare_unit_number)
{
	uint64_t                    data_unit_id_out;
	struct m0_fid               cobfid;
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_pdclust_instance *pi;
	enum m0_pool_nd_state       state_out;
	bool                        result = true;
	int                         rc;

	rc = m0_sns_cm_fid_layout_instance(pl, &pi, fid);
	M0_ASSERT(rc == 0);

	if (m0_pdclust_unit_classify(pl, spare_unit_number) == M0_PUT_SPARE) {
		/*
		 * Firstly, check if the device corresponding to the given spare
		 * unit is already repaired. If yes then the spare unit is empty
		 * or the data is already moved. So we need not repair the spare
		 * unit.
		 */
		M0_SET0(&sa);
		M0_SET0(&ta);
		sa.sa_unit = spare_unit_number;
		sa.sa_group = group_number;
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
		rc = m0_sns_repair_data_map(scm->sc_base.cm_pm, fid, pl,
				group_number, spare_unit_number,
				&data_unit_id_out);
		if (rc == -ENOENT)
			goto out;

		M0_SET0(&sa);
		M0_SET0(&ta);

		sa.sa_unit = data_unit_id_out;
		sa.sa_group = group_number;

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
	m0_layout_instance_fini(&pi->pi_base);
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
					  struct m0_fid *cobfid)
{
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_fid               gobfid;
        struct m0_pdclust_instance *pi;
	int                         rc;

        agid2fid(&sag->sag_base.cag_id, &gobfid);
        rc = m0_sns_cm_fid_layout_instance(pl, &pi, &gobfid);
        if (rc != 0)
                return -ENOENT;
	sa.sa_group = agid2group(&sag->sag_base.cag_id);
	sa.sa_unit  = tgt_unit;
	m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, &gobfid, cobfid);
	m0_layout_instance_fini(&pi->pi_base);

	return 0;
}

/**
 * Fetches file size and layout for given gob_fid.
 * The layout fetched here should be finalised using m0_layout_put()
 * byt the caller of this function.
 * @note This may block.
 * @retval 0 on success, IT_WAIT for blocking operation
 */
M0_INTERNAL int
m0_sns_cm_file_size_layout_fetch(struct m0_cm *cm,
				 struct m0_fid *gfid,
				 struct m0_pdclust_layout **layout,
				 uint64_t *fsize)
{
        struct m0_cob_attr        attr = { {0} };
        struct m0_layout_domain  *ldom;
        struct m0_pdclust_layout *pdl = NULL;
        struct m0_layout         *l = NULL;
	struct m0_reqh           *reqh = cm->cm_service.rs_reqh;
        int                       rc;

        M0_PRE(cm != NULL && gfid != NULL && layout != NULL && fsize != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	/* Support for UT, avoids get/set attributes for COB. */
	if (M0_FI_ENABLED("ut_layout_fsize_fetch"))
		return sns_cm_ut_file_size_layout_fetch(cm, gfid, layout, fsize);

        ldom = &reqh->rh_ldom;

        M0_LOG(M0_DEBUG, "fetch file size and layout for "FID_F, FID_P(gfid));
        rc = m0_ios_mds_getattr(reqh, gfid, &attr);
        if (rc == 0) {
                M0_ASSERT(attr.ca_valid | M0_COB_LID);
                M0_ASSERT(attr.ca_valid | M0_COB_SIZE);
                *fsize = attr.ca_size;
                M0_LOG(M0_DEBUG, "FID = "FID_F", size = %llu, lid = %llu",
		       FID_P(gfid),
		       (unsigned long long)attr.ca_size,
		       (unsigned long long)attr.ca_lid);
                rc = m0_ios_mds_layout_get(reqh, ldom, attr.ca_lid, &l);
                if (rc == 0) {
                        pdl = m0_layout_to_pdl(l);
                        M0_LOG(M0_DEBUG, "pdl N=%d,K=%d,P=%d,"
                                         "unit_size=%llu",
                                         m0_pdclust_N(pdl),
                                         m0_pdclust_K(pdl),
                                         m0_pdclust_P(pdl),
                                         (unsigned long long)
                                         m0_pdclust_unit_size(pdl));

                        *layout = pdl;
                } else
                        M0_LOG(M0_DEBUG, "getlayout for %llu failed rc = %d",
                                         (unsigned long long)attr.ca_lid, rc);
        } else
                M0_LOG(M0_ERROR, "getattr for "FID_F" failed rc = %d",
		       FID_P(gfid), rc);
        return rc;
}


/**
 * Callback for async getattr.
 *
 * This callback should tell the original cm the result and post event to let it
 * continue execution.
 *
 * Note: The following code are only for sample purpose to use async getattr.
 *
 */
static void getattr_callback(void *arg, int rc)
{
	struct m0_cm *cm = arg;

	if (rc == 0) {
		/* getattr succeeded. */
	}
	/* TODO:
	 * To inform the original caller to continue execution.
	 */
	(void)cm;
}

/**
 * getattr sync
 *
 * @retval 0, succeeded to sent out the request asynchronously.
 *            Caller of this function should relinquish current execution,
 *            i.e. return M0_FSO_WAIT to reqh.
 * @retval non-zero, failure happened. No callback will be issued.
 *            Caller should continue handling error case.
 *
 * XXX: maybe a pointer to the current fom is needed here as an argument.
 *      So the callback can wake up the fom.
 */
M0_INTERNAL int
m0_sns_cm_file_attr_fetch_async(struct m0_cm *cm,
				struct m0_fid *gfid,
				struct m0_cob_attr *attr)
{
	struct m0_reqh *reqh = cm->cm_service.rs_reqh;
	int             rc;

	M0_PRE(cm != NULL);
	M0_PRE(gfid != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	rc = m0_ios_mds_getattr_async(reqh, gfid, attr, &getattr_callback, cm);
	return rc;
}

static void layout_callback(void *arg, int rc)
{
	struct m0_cm *cm = arg;

	if (rc == 0) {
		/* getlayout succeeded. */
	}
	/* TODO:
	 * To inform the original caller to continue execution.
	 */
	(void)cm;
}

/**
 * getlayout async.
 */
M0_INTERNAL int
m0_sns_cm_file_layout_fetch_async(struct m0_cm *cm,
				  uint64_t lid,
				  struct m0_layout **l_out)
{
	struct m0_layout_domain  *ldom;
	struct m0_reqh           *reqh = cm->cm_service.rs_reqh;
	int                       rc;

	M0_PRE(cm != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	ldom = &reqh->rh_ldom;

	rc = m0_ios_mds_layout_get_async(reqh, ldom, lid, l_out,
					 &layout_callback, cm);
	return rc;
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

	M0_PRE(scm != NULL && pl != NULL);

	upg = m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
	sa.sa_group = group;
	for (unit = 0; unit < upg; ++unit) {
		if (m0_sns_cm_unit_is_spare(scm, pl, gfid, group, unit))
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

	return group_failures;
}

M0_INTERNAL bool m0_sns_cm_ag_is_relevant(struct m0_sns_cm *scm,
                                          struct m0_pdclust_layout *pl,
                                          const struct m0_cm_ag_id *id)
{
        struct m0_pdclust_instance *pi;
        struct m0_fid               fid;
        size_t                      group_failures;
        uint64_t                    group;
        int                         rc;
        bool                        result = false;

        agid2fid(id,  &fid);
        rc = m0_sns_cm_fid_layout_instance(pl, &pi, &fid);
        if (rc == 0) {
                group = id->ai_lo.u_lo;
                /* Firstly check if this group has any failed units. */
                group_failures = m0_sns_cm_ag_failures_nr(scm, &fid, pl,
                                                         pi, group, NULL);
                if (group_failures > 0 )
			result = scm->sc_helpers->sch_ag_is_relevant(scm, &fid,
								     group, pl,
								     pi);
                m0_layout_instance_fini(&pi->pi_base);
        }

        return result;
}

M0_INTERNAL uint64_t
m0_sns_cm_ag_max_incoming_units(const struct m0_sns_cm *scm,
				const struct m0_cm_ag_id *id,
				struct m0_pdclust_layout *pl)
{
	M0_PRE(m0_cm_is_locked(&scm->sc_base));

	return scm->sc_helpers->sch_ag_max_incoming_units(scm, id, pl);
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
