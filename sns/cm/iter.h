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

#pragma once

#ifndef __MERO_SNS_CM_ITER_H__
#define __MERO_SNS_CM_ITER_H__

#include "sm/sm.h"

#include "cob/ns_iter.h"
#include "layout/linear_enum.h"

/**
  @addtogroup SNSCM
  @{
*/

struct m0_cm;
struct m0_sns_cm;
struct m0_sns_cm_ag;
struct m0_cm_cp;

/**
 * File context in copy machine.
 * This maintains details like, the pdclust layout of the GOB, its corresponding
 * parity group, unit in the parity group which is being processed. Also few
 * more details regarding the current file size, number of units per group, etc.
 */
struct m0_sns_cm_file_context {
	/** GOB being re-structured. */
	struct m0_fid                 sfc_gob_fid;

	size_t                        sfc_fsize;

	/** GOB layout. */
	struct m0_pdclust_layout     *sfc_pdlayout;

	/** pdclust instance for a particular GOB. */
	struct m0_pdclust_instance   *sfc_pi;

	/** Total number of units (i.e. N + 2K) in a parity group. */
	uint32_t                      sfc_upg;

	/** Total number of data and parity units in a parity group. */
	uint32_t                      sfc_dpupg;

	/** Total number of parity groups in file. */
	uint64_t                      sfc_groups_nr;

	/**
	 * Unit within a particular parity group corresponding to
	 * m0_sns_cm_iter::si_gob_fid, of which the data is to be read or
	 * written.
	 */
	struct m0_pdclust_src_addr    sfc_sa;

	/**
	 * COB index and frame number in the COB, corresponding to
	 * m0_sns_cm_file_context::sfc_sa.
	 */
	struct m0_pdclust_tgt_addr    sfc_ta;

	/**
	 * Total number of failed units in an aggregation group represented by
	 * m0_sns_cm_file_context::sfc_sa.sa_group
	 */
	uint64_t                      sfc_group_nr_fail_units;

	/** COB fid corresponding to m0_sns_cm_file_context::sfc_ta. */
	struct m0_fid                 sfc_cob_fid;

	bool                          sfc_cob_is_spare_unit;
};

/**
 * SNS copy machine data iterator. This iterates through the local data objects
 * which are part of the re-structuring process, in-order to recover from a
 * particular failure. SNS copy machine data iterator is implemented as a state
 * machine. This is invoked from the copy packet pump FOM which uses the non-
 * blocking infrastructure, thus making the iterator non-blocking.
 * @see struct m0_cm_cp_pump
 */
struct m0_sns_cm_iter {
	/** Iterator state machine. */
	struct m0_sm                     si_sm;

        /**
         * Failure data received in trigger FOP.
         * This is set when a TRIGGER FOP is received. For SNS repair, this
         * will be the failed container id.
         * SNS copy machine data iterator assumes this to be set before invoking
         * m0_sns_cm_iter_next().
         */
        uint64_t                        *si_fdata;

	struct m0_dbenv                 *si_dbenv;

	struct m0_cob_domain            *si_cob_dom;

	/** Layout details of a file. */
	struct m0_sns_cm_file_context    si_fc;

	/**
	 * Saved pre allocated copy packet, which needs to be configured.
	 * This is allocated by the copy machine pump FOM.
	 */
	struct m0_sns_cm_cp             *si_cp;

	/** Cob fid namespace iterator. */
	struct m0_cob_fid_ns_iter        si_cns_it;

	uint64_t                         si_magix;
};

M0_INTERNAL int m0_sns_cm_iter_init(struct m0_sns_cm_iter *it);
M0_INTERNAL void m0_sns_cm_iter_fini(struct m0_sns_cm_iter *it);

M0_INTERNAL int m0_sns_cm_iter_start(struct m0_sns_cm_iter *it);
M0_INTERNAL void m0_sns_cm_iter_stop(struct m0_sns_cm_iter *it);

/**
 * Iterates over parity groups in global fid order, calculates next data or
 * parity unit from the parity group to be read, calculates cob fid for the
 * parity unit, creates and initialises new aggregation group corresponding
 * to the parity group if required, and fills this information in the given
 * copy packet. After initialising copy packet with the stob details, an empty
 * buffer from the struct m0_sns_cm::rc_obp buffer pool is attached to
 * the copy packet.
 */
M0_INTERNAL int m0_sns_cm_iter_next(struct m0_cm *cm, struct m0_cm_cp *cp);

/**
 * Calculates fid of the COB containing the spare unit, and its index into the
 * COB for the given failure index in the aggregation group.
 *
 * @see m0_sns_cm_ag::sag_tgts
 * @see m0_sns_cm_ag_tgt_addr::tgt_cob_index
 * @see m0_sns_cm_ag_tgt_addr::tgt_cobfid
 */
M0_INTERNAL void m0_sns_cm_iter_tgt_unit_to_cob(struct m0_sns_cm_ag *rag);

M0_INTERNAL uint64_t
m0_sns_cm_iter_failures_nr(const struct m0_sns_cm_iter *it);

M0_INTERNAL ssize_t m0_sns_cm_iter_file_size(struct m0_fid *gfid);

/** @} SNSCM */
#endif /* __MERO_SNS_CM_ITER_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
