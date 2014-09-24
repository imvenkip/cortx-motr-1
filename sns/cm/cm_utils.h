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

#pragma once

#ifndef __MERO_SNS_CM_UTILS_H__
#define __MERO_SNS_CM_UTILS_H__

#include "layout/pdclust.h"

#include "sns/cm/cm.h"

/**
   @addtogroup SNSCM

   @{
*/

struct m0_cob_domain;
struct m0_cm;
struct m0_sns_cm;
struct m0_sns_cm_ag;

/**
 * Returns cob fid for the sa->sa_unit.
 * @see m0_pdclust_instance_map
 */
M0_INTERNAL void
m0_sns_cm_unit2cobfid(struct m0_pdclust_layout *pl,
		      struct m0_pdclust_instance *pi,
		      const struct m0_pdclust_src_addr *sa,
		      struct m0_pdclust_tgt_addr *ta,
		      const struct m0_fid *gfid,
		      struct m0_fid *cfid_out);

M0_INTERNAL uint64_t m0_sns_cm_ag_unit2cobindex(struct m0_sns_cm_ag *sag,
						uint64_t unit,
						struct m0_pdclust_layout *pl);

M0_INTERNAL uint64_t m0_sns_cm_nr_groups(struct m0_pdclust_layout *pl,
					 uint64_t fsize);


/**
 * Searches for given cob_fid in the local cob domain.
 */
M0_INTERNAL int m0_sns_cm_cob_locate(struct m0_cob_domain *cdom,
				     const struct m0_fid *cob_fid);


/**
 * Calculates number of local data units for a given parity group.
 * This is invoked when new struct m0_sns_cm_ag instance is allocated, from
 * m0_cm_aggr_group_alloc(). This is done in context of sns copy machine data
 * iterator during the latter's ITPH_CP_SETUP phase. Thus we need not calculate
 * the new GOB layout and corresponding pdclust instance, instead used the ones
 * already calculated and save in the iterator, but we take GOB fid and group
 * number as the parameters to this function in-order to perform sanity checks.
 */
M0_INTERNAL uint64_t m0_sns_cm_ag_nr_local_units(struct m0_sns_cm *scm,
						 const struct m0_fid *fid,
						 struct m0_pdclust_layout *pl,
						 uint64_t group);


M0_INTERNAL uint64_t m0_sns_cm_ag_nr_global_units(const struct m0_sns_cm_ag *ag,
						  struct m0_pdclust_layout *pl);

M0_INTERNAL uint64_t
m0_sns_cm_ag_max_incoming_units(const struct m0_sns_cm *scm,
				const struct m0_cm_ag_id *id,
				struct m0_pdclust_layout *pl);

/**
 * Builds layout instance for new GOB fid calculated in ITPH_FID_NEXT phase.
 * @see iter_fid_next()
 */
M0_INTERNAL int m0_sns_cm_fid_layout_instance(struct m0_pdclust_layout *pl,
					      struct m0_pdclust_instance **pi,
					      const struct m0_fid *fid);

M0_INTERNAL bool m0_sns_cm_is_cob_failed(const struct m0_sns_cm *scm,
					 const struct m0_fid *cob_fid);

M0_INTERNAL bool m0_sns_cm_is_cob_repaired(const struct m0_sns_cm *scm,
					   const struct m0_fid *cob_fid);

/**
 * Returns index of spare unit in the parity group, given the failure index
 * in the group.
 */
M0_INTERNAL uint64_t
m0_sns_cm_ag_spare_unit_nr(const struct m0_pdclust_layout *pl,
			   uint64_t fidx);

M0_INTERNAL bool m0_sns_cm_unit_is_spare(const struct m0_sns_cm *scm,
                                         struct m0_pdclust_layout *pl,
                                         const struct m0_fid *fid,
                                         uint64_t group_number,
                                         uint64_t spare_unit_number);

/**
 * Returns starting index of the unit in the aggregation group relevant to
 * the sns copy machine operation.
 * @see m0_sns_cm_op
 */
M0_INTERNAL uint64_t m0_sns_cm_ag_unit_start(const struct m0_sns_cm *scm,
					     const struct m0_pdclust_layout *pl);

/**
 * Returns end index of the unit in the aggregation group relevant to the
 * sns copy machine operation.
 * @see m0_sns_cm_op
 */
M0_INTERNAL uint64_t m0_sns_cm_ag_unit_end(const struct m0_sns_cm *scm,
					   const struct m0_pdclust_layout *pl);

/**
 * Calculates and returns the cobfid for the given group and the target unit
 * of the file (represented by the gobfid).
 */
M0_INTERNAL int m0_sns_cm_ag_tgt_unit2cob(struct m0_sns_cm_ag *sag,
					  uint64_t tgt_unit,
					  struct m0_pdclust_layout *pl,
					  struct m0_fid *cobfid);

/**
 * Fetches file size and layout for given gob_fid.
 * @note This may block.
 * @retval 0 on success, IT_WAIT for blocking operation
 */
M0_INTERNAL int m0_sns_cm_file_size_layout_fetch(struct m0_cm *cm,
						 struct m0_fid *gfid,
						 struct m0_pdclust_layout
						 **layout, uint64_t *fsize);

M0_INTERNAL const char *m0_sns_cm_tgt_ep(struct m0_cm *cm,
					 struct m0_fid *gfid);

M0_INTERNAL size_t m0_sns_cm_ag_failures_nr(const struct m0_sns_cm *scm,
					    const struct m0_fid *gfid,
					    struct m0_pdclust_layout *pl,
					    struct m0_pdclust_instance *pi,
					    uint64_t group,
					    struct m0_bitmap *fmap_out);

/**
 * Returns true if the given aggregation group corresponding to the id is
 * relevant. Thus if a node hosts the spare unit of the given aggregation group
 * and which is not the failed unit of the group, the group is considered for
 * the repair.
 */
M0_INTERNAL bool m0_sns_cm_ag_is_relevant(struct m0_sns_cm *scm,
					  struct m0_pdclust_layout *pl,
					  const struct m0_cm_ag_id *id);

M0_INTERNAL bool
m0_sns_cm_ag_relevant_is_done(const struct m0_cm_aggr_group *ag,
			      uint64_t nr_cps_fini);

M0_INTERNAL bool m0_sns_cm_fid_is_valid(const struct m0_fid *fid);

/** @} endgroup SNSCM */

/* __MERO_SNS_CM_UTILS_H__ */

#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
