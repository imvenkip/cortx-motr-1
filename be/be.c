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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 3-Jun-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/be.h"
#include "be/tx_group_fom.h"
#include "be/tx_internal.h"

/**
 * @addtogroup be
 *
 * This file contains implementation details of BE features.
 *
 * Table of contents
 * - Captured regions lifecycle: capturing, seg and log I/O
 *
 * @section Captured regions lifecycle: capturing, seg and log I/O
 *
 * - Each segment change (by user) is followed by m0_be_tx_capture() (called by
 *   user);
 * - m0_be_tx_capture() does the following:
 *   - determines generation index of the region;
 *   - captures given region into transactions's reg_area. It is done by copying
 *     region data into tx reg_area;
 * - transaction is closed and grouped;
 * - tx group has it's own reg_area;
 * - group reg_area is a merge of all group tx-es reg_areas. Merge is done using
 *   m0_be_reg_area_merger. Merge rules:
 *   - if a region is in some tx reg_area from the group then the region is in
 *     group reg_area;
 *   - group reg_area contains regions with largest generation index among all
 *     transactions from the group;
 * - BE engine has reg_area called global reg_area. It contains merge of all
 *   group reg_areas. Global reg_area properties:
 *   - it has all regions of all group reg_areas with the latest generation
 *     index;
 *   - it is updated after group reg_area is filled by m0_be_reg_area_merger;
 *   - it is pruned after update. All regions with generation index less than
 *     lowest generation index of first captured region among all non-finalized
 *     transaction are removed from the global reg_area;
 * - group reg_area is logged as is;
 * - group reg_area is changed before placing taking into account the global
 *   reg_area. Regions from group reg_area that exist in global reg_area and
 *   have generation index less than corresponding region from the global
 *   reg_area are not placed - the new region data was or going to be placed
 *   (because the region is in the global reg_area).
 * @{
 */

M0_INTERNAL int m0_backend_init(void)
{
	return m0_be_tx_mod_init() ?: (m0_be_tx_group_fom_mod_init(), 0);
}

M0_INTERNAL void m0_backend_fini(void)
{
	m0_be_tx_group_fom_mod_fini();
	m0_be_tx_mod_fini();
}

/** @} end of be group */
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
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
