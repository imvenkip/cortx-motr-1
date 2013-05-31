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
 * Original creation date: 03/08/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/misc.h"
#include "lib/trace.h"

#include "cob/cob.h"
#include "mero/setup.h"
#include "ioservice/io_service.h"

#include "sns/cm/ag.h"
#include "sns/cm/iter.h"
#include "sns/cm/cm_utils.h"

/**
   @addtogroup SNSCM

   @{
 */

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

M0_INTERNAL int m0_sns_cm_cob_locate(struct m0_dbenv *dbenv,
				struct m0_cob_domain *cdom,
				const struct m0_fid *cob_fid)
{
	struct m0_cob        *cob;
	struct m0_cob_oikey   oikey;
	struct m0_db_tx       tx;
	int                   rc;

	rc = m0_db_tx_init(&tx, dbenv, 0);
	if (rc != 0)
		return rc;
	m0_cob_oikey_make(&oikey, cob_fid, 0);
	rc = m0_cob_locate(cdom, &oikey, M0_CA_NSKEY_FREE, &cob, &tx);
	if (rc == 0) {
		M0_ASSERT(m0_fid_eq(cob_fid, cob->co_fid));
		m0_db_tx_commit(&tx);
		m0_cob_put(cob);
	} else
		m0_db_tx_abort(&tx);

	return rc;
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
	start = m0_sns_cm_ag_unit_start(scm->sc_op, pl);
	end = m0_sns_cm_ag_unit_end(scm->sc_op, pl);
	sa.sa_group = group;
	for (i = start; i < end; ++i) {
		sa.sa_unit = i;
		M0_SET0(&ta);
		M0_SET0(&cobfid);
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, fid, &cobfid);
		rc = m0_sns_cm_cob_locate(it->si_dbenv, it->si_cob_dom,
					  &cobfid);
		if (rc == 0 && !m0_sns_cm_is_cob_failed(scm, &cobfid))
			M0_CNT_INC(nrlu);
	}
	m0_layout_instance_fini(&pi->pi_base);

	return nrlu;
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

M0_INTERNAL bool m0_sns_cm_is_cob_failed(struct m0_sns_cm *scm,
					 const struct m0_fid *cob_fid)
{
	int i;

	for (i = 0; i < scm->sc_failures_nr; ++i) {
		if (cob_fid->f_container == scm->sc_it.si_fdata[i])
			return true;
	}

	return false;
}

M0_INTERNAL uint64_t m0_sns_cm_ag_spare_unit_nr(const struct m0_pdclust_layout *pl,
						uint64_t fidx)
{
        return m0_pdclust_N(pl) + m0_pdclust_K(pl) + fidx;
}

M0_INTERNAL uint64_t m0_sns_cm_ag_unit_start(enum m0_sns_cm_op op,
					     struct m0_pdclust_layout *pl)
{
        switch (op) {
        case SNS_REPAIR:
                /* Start from 0th unit of the group. */
                return 0;
        case SNS_REBALANCE:
                /* Start from the first spare unit of the group. */
                return m0_sns_cm_ag_spare_unit_nr(pl, 0);
        default:
                 M0_IMPOSSIBLE("op");
        }

        return ~0;
}

M0_INTERNAL uint64_t m0_sns_cm_ag_unit_end(enum m0_sns_cm_op op,
					   struct m0_pdclust_layout *pl)
{

        switch (op) {
        case SNS_REPAIR:
                /* End at the last data/parity unit of the group. */
                return m0_pdclust_N(pl) + m0_pdclust_K(pl);
        case SNS_REBALANCE:
                /* End at the last spare unit of the group. */
                return m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
        default:
                M0_IMPOSSIBLE("op");
        }

        return ~0;
}

/**
 * Returns the index of the failed data/parity unit in the parity group.
 * The same offset of the data/parity unit in the group on the failed device is
 * used to copy data from the spare unit to the new device by re-balance
 * operation.
 */
static uint64_t __group_failed_unit_index(struct m0_pdclust_layout *pl,
					  struct m0_pdclust_instance *pi,
					  struct m0_fid *gfid, uint64_t group,
					  uint64_t fdata)
{
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_fid               cobfid;
	uint64_t                    dpupg = m0_pdclust_N(pl) + m0_pdclust_K(pl);
	int                         i;

	sa.sa_group = group;
	for (i = 0; i < dpupg; ++i) {
		sa.sa_unit = i;
		M0_SET0(&ta);
		M0_SET0(&cobfid);
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, gfid, &cobfid);
		if (cobfid.f_container == fdata)
			return i;
	}

	return ~0;
}

static uint64_t __target_unit_nr(struct m0_pdclust_layout *pl,
				 struct m0_pdclust_instance *pi,
				 struct m0_fid *gfid, uint64_t group,
				 uint64_t fdata, enum m0_sns_cm_op op,
				 uint64_t fidx)
{
	switch (op) {
	case SNS_REPAIR:
		return m0_sns_cm_ag_spare_unit_nr(pl, fidx);
	case SNS_REBALANCE:
		return __group_failed_unit_index(pl, pi, gfid, group, fdata);
	default:
		M0_IMPOSSIBLE("op");
	}

	return ~0;
}

M0_INTERNAL int m0_sns_cm_ag_tgt_unit2cob(struct m0_sns_cm_ag *sag,
					  struct m0_pdclust_layout *pl)
{
	struct m0_sns_cm                *scm = cm2sns(sag->sag_base.cag_cm);
	struct m0_pdclust_src_addr       sa;
	struct m0_pdclust_tgt_addr       ta;
	struct m0_fid                    gobfid;
	struct m0_fid                    cobfid;
	struct m0_pdclust_instance      *pi;
	uint64_t                         fidx;
	int                              rc;

	agid2fid(&sag->sag_base.cag_id, &gobfid);
	sa.sa_group = agid2group(&sag->sag_base.cag_id);
	rc = m0_sns_cm_fid_layout_instance(pl, &pi, &gobfid);
	if (rc == 0) {
		for (fidx = 0; fidx < sag->sag_fnr; ++fidx) {
			M0_SET0(&ta);
			M0_SET0(&cobfid);
			sa.sa_unit  = __target_unit_nr(pl, pi, &gobfid, sa.sa_group,
						       scm->sc_it.si_fdata[fidx],
						       scm->sc_op, fidx);
			m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, &gobfid, &cobfid);
			sag->sag_fc[fidx].fc_tgt_cobfid = cobfid;
			sag->sag_fc[fidx].fc_tgt_cob_index = ta.ta_frame *
							     m0_pdclust_unit_size(pl);
		}
		m0_layout_instance_fini(&pi->pi_base);
	}

	return rc;
}

M0_INTERNAL struct m0_pdclust_layout *
m0_sns_cm_fid2layout(struct m0_sns_cm *scm, struct m0_fid *gfid)
{
	return scm->sc_it.si_fc.sfc_pdlayout;
}

/**
 * Fetches file size and layout for given gob_fid.
 * The layout fetched here should be finalised using m0_layout_put()
 * byt the caller of this function.
 * @note This may block.
 * @retval 0 on success, IT_WAIT for blocking operation
 */
M0_INTERNAL int m0_sns_cm_file_size_layout_fetch(struct m0_cm *cm,
						 struct m0_fid *gfid,
						 struct m0_pdclust_layout
						 **layout, uint64_t *fsize)
{
        struct m0_cob_attr        attr = { {0} };
        struct m0_layout_domain  *ldom;
        struct m0_pdclust_layout *pdl = NULL;
        struct m0_layout         *l = NULL;
	struct m0_reqh           *reqh = cm->cm_service.rs_reqh;
        int                       rc;

        M0_PRE(cm != NULL && gfid != NULL && layout != NULL && fsize != NULL);
	M0_PRE(m0_cm_is_locked(cm));

        ldom = &reqh->rh_ldom;

        M0_LOG(M0_DEBUG, "fetch file size and layout for %llu:%llu",
                         (unsigned long long)gfid->f_container,
                         (unsigned long long)gfid->f_key);
        rc = m0_ios_mds_getattr(reqh, gfid, &attr);
        if (rc == 0) {
                M0_ASSERT(attr.ca_valid | M0_COB_LID);
                M0_ASSERT(attr.ca_valid | M0_COB_SIZE);
                *fsize = attr.ca_size;
                M0_LOG(M0_DEBUG, "FID = %llu:%llu, size = %llu, lid = %llu",
                                 (unsigned long long)gfid->f_container,
                                 (unsigned long long)gfid->f_key,
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
                M0_LOG(M0_ERROR, "getattr for %llu:%llu failed rc = %d",
                                 (unsigned long long)gfid->f_container,
                                 (unsigned long long)gfid->f_key, rc);
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
		M0_LOG(M0_DEBUG, "Target endpoint for: %llu:%llu %s",
				 (unsigned long long)cfid->f_container,
				 (unsigned long long)cfid->f_key,
				 ex->ex_endpoint);
		return ex->ex_endpoint;
	} m0_tl_endfor;

	return NULL;
}


M0_INTERNAL int m0_sns_cm_cob_is_local(struct m0_fid *cobfid,
					struct m0_dbenv *dbenv,
					struct m0_cob_domain *cdom)
{
	return m0_sns_cm_cob_locate(dbenv, cdom, cobfid);
}

M0_INTERNAL size_t m0_sns_cm_ag_failures_nr(struct m0_sns_cm *scm,
					    struct m0_fid *gfid,
					    struct m0_pdclust_layout *pl,
					    struct m0_pdclust_instance *pi,
					    uint64_t group)
{
	struct m0_pdclust_src_addr sa;
	struct m0_pdclust_tgt_addr ta;
	struct m0_fid              cobfid;
	uint64_t                   dpupg;
	uint64_t                   unit;
	size_t                     group_failures = 0;
	int                        i;

	M0_PRE(scm != NULL && pl != NULL);

	dpupg = m0_pdclust_N(pl) + m0_pdclust_K(pl);
	for (unit = 0; unit < dpupg; ++unit) {
		sa.sa_unit = unit;
		sa.sa_group = group;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, gfid, &cobfid);
		for (i = 0; i < scm->sc_failures_nr; ++i) {
			if (cobfid.f_container == scm->sc_it.si_fdata[i])
				M0_CNT_INC(group_failures);
		}
	}

	return group_failures;
}

M0_INTERNAL bool m0_sns_cm_ag_is_relevant(struct m0_sns_cm *scm,
                                          struct m0_pdclust_layout *pl,
                                          const struct m0_cm_ag_id *id)
{
	struct m0_sns_cm_iter      *it = &scm->sc_it;
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_pdclust_instance *pi;
	struct m0_fid               fid;
	struct m0_fid               cobfid;
	size_t                      group_failures;
	int                         rc;

	agid2fid(id,  &fid);

	rc = m0_sns_cm_fid_layout_instance(pl, &pi, &fid);
	if (rc == 0) {
		/* Firstly check if this group has any failed units. */
		group_failures = m0_sns_cm_ag_failures_nr(scm, &fid, pl,
							  pi, id->ai_lo.u_lo);
		if (group_failures > 0) {
			sa.sa_group = id->ai_lo.u_lo;
			sa.sa_unit = m0_pdclust_N(pl) + m0_pdclust_K(pl);
			m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, &fid, &cobfid);
			rc = m0_sns_cm_cob_locate(it->si_dbenv, it->si_cob_dom,
						  &cobfid);
			if (rc == 0 && !m0_sns_cm_is_cob_failed(scm, &cobfid))
				return true;
		}
		m0_layout_instance_fini(&pi->pi_base);
	}

	return false;
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
