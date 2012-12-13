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

#include "ioservice/cobfid_map.h"
#include "layout/linear_enum.h"

/**
  @addtogroup SNSCM
  @{
*/

struct m0_sns_cm;
struct m0_sns_cm_ag;

/**
 * PDclust Layout details for a GOB (file).
 * This maintains details like, the pdclust layout of the GOB, its corresponding
 * parity group, unit in the parity group which is being processed. Also few
 * more details regarding the current file size, number of units per group, etc.
 */
struct m0_sns_cm_pdclust_layout {
	/** GOB being re-structured. */
	struct m0_fid                 spl_gob_fid;

	size_t                        spl_fsize;

	/** GOB layout. */
	struct m0_pdclust_layout     *spl_base;

	struct m0_layout_linear_enum *spl_le;

	/** pdclust instance for a particular GOB. */
	struct m0_pdclust_instance   *spl_pi;

	/** Total number of units (i.e. N + 2K) in a parity group. */
	uint32_t                      spl_upg;

	/** Number of data units in the parity group. */
	uint32_t                      spl_N;

	/** Number of parity units in the parity group. */
	uint32_t                      spl_K;

	/** Total pool width. */
	uint32_t                      spl_P;

	/** Total number of data and parity units in a parity group. */
	uint32_t                      spl_dpupg;

	/** Total number of parity groups in file. */
	uint64_t                      spl_groups_nr;

	/**
	 * Unit within a particular parity group corresponding to
	 * m0_sns_cm_iter::si_gob_fid, of which the data is to be read or
	 * written.
	 */
	struct m0_pdclust_src_addr    spl_sa;

	/**
	 * COB index and frame number in the COB, corresponding to
	 * m0_sns_cm_iter::si_sa.
	 */
	struct m0_pdclust_tgt_addr    spl_ta;

	/** COB fid corresponding to m0_sns_cm_iter::si_ta. */
	struct m0_fid                 spl_cob_fid;

	bool                          spl_cob_is_spare_unit;
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
        uint64_t                         si_fdata;

	struct m0_dbenv                 *si_dbenv;

	struct m0_cob_domain            *si_cob_dom;

        /*
         * XXX Temporary location for layout domain required to build pdclust
         * layout.
         */
        struct m0_layout_domain          si_lay_dom;

	/** Layout details of a file. */
	struct m0_sns_cm_pdclust_layout  si_pl;

	/**
	 * Saved pre allocated copy packet, which needs to be configured.
	 * This is allocated by the copy machine pump FOM.
	 */
	struct m0_sns_cm_cp             *si_cp;

	uint64_t                         si_magix;
};

M0_INTERNAL int m0_sns_cm_iter_init(struct m0_sns_cm_iter *it);
M0_INTERNAL void m0_sns_cm_iter_fini(struct m0_sns_cm_iter *it);

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
 * Calculates number of local data units for a given parity group.
 * This is invoked when new struct m0_sns_cm_ag instance is allocated, from
 * m0_sns_cm_ag_find(). This is done in context of sns copy machine data
 * iterator during the latter's ITPH_CP_SETUP phase. Thus we need not calculate
 * the new GOB layout and corresponding pdclust instance, instead used the ones
 * already calculated and save in the iterator, but we take GOB fid and group
 * number as the parameters to this function in-order to perform sanity checks.
 */
M0_INTERNAL uint64_t nr_local_units(struct m0_sns_cm *scm,
				    const struct m0_fid *fid, uint64_t group);

/**
 * Calculates fid of the COB containing the spare unit, and its index into the
 * COB for the given aggregation group.
 *
 * @see m0_sns_cm_ag::sag_spare_cobfid
 * @see m0_sns_cm_ag::sag_spare_cob_index
 */
M0_INTERNAL void target_unit_to_cob(struct m0_sns_cm_ag *rag);

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
