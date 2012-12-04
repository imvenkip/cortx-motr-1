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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 10/22/2010
 */

#pragma once

#ifndef __MERO_DTM_VERNO_H__
#define __MERO_DTM_VERNO_H__

#include "lib/types.h"            /* uint64_t */
#include "fol/lsn.h"              /* m0_lsn_t */
#include "xcode/xcode_attr.h"     /* M0_XCA_RECORD */

/**
   @addtogroup dtm Distributed transaction manager
   @{
 */

/* import */
struct m0_fol_rec;

/* export */
struct m0_verno;
typedef uint64_t m0_vercount_t;

/**
   Version number.

   See HLD referenced below for overall description of version numbers.

   Version numbers identify and order unit states, where a unit is a piece of
   file system data or meta-data which is updated serially (i.e., supports no
   concurrent updates). Division of file system state into units is determined
   by a trade-off between an increase in achievable concurrency levels (by
   making units smaller) and an overhead of tracking individual units (reduced
   by making units coarser). Typically, a unit is something that can be
   protected by a DLM lock. For example, an inode could be a unit (or few units,
   like in Lustre, where two locks are associated with an inode). A data block
   in a file could be a unit. A directory page or an individual directory entry
   could be units.

   Generic version number code does not depend on specifics of unit definitions,
   beyond assuming that a version number is stored with every unit.

   @see https://docs.google.com/a/xyratex.com/Doc?docid=0AQaCw6YRYSVSZGZmMzV6NzJfMGZoZm10NmRy
 */
struct m0_verno {
	/** a reference to a fol record that brought the unit into this
	    version. */
	m0_lsn_t      vn_lsn;
	/** an ordinal number of this version in the unit's serial history. */
	m0_vercount_t vn_vc;
} M0_XCA_RECORD;

/**
   Version number comparison function.

   Compares version numbers of the same unit.

   @retval -1 when vn0 is earlier than vn1 in the unit's serial history
   @retval  0 when vn0 equals vn1
   @retval +1 when vn0 is later than vn1 in the unit's serial history
 */
M0_INTERNAL int m0_verno_cmp(const struct m0_verno *vn0,
			     const struct m0_verno *vn1);

/**
   Checks whether a unit update with a before-version-number @before_update can
   be applied to a unit with the version number @unit.

   If @total is false, the update is "differential", otherwise it completely
   overwrites the unit.

   @retval 0 the update can be applied right away

   @retval -EALREADY the update is already present in the unit's state

   @retval -EAGAIN the update can be applied, but not immediately: some
   intermediate updates are missing.

   @see m0_verno_is_undoable()
 */
M0_INTERNAL int m0_verno_is_redoable(const struct m0_verno *unit,
				     const struct m0_verno *before_update,
				     bool total);

/**
   Checks whether a unit update with a before-version-number @before_update can
   be undone to a unit with the version number @unit.

   If @total is false, the update is "differential", otherwise it completely
   overwrites the unit.

   @retval 0 the update can be undone right away

   @retval -EALREADY the update has already been undone, or was never applied in
   the first place.

   @retval -EAGAIN the update can be undone, but not immediately: some
   intermediate updates have to be undone first.

   @see m0_verno_is_redoable()
 */
M0_INTERNAL int m0_verno_is_undoable(const struct m0_verno *unit,
				     const struct m0_verno *before_update,
				     bool total);

/**
   Checks that version number comparison invariant, described in the HLD, holds.
 */
M0_INTERNAL int m0_verno_cmp_invariant(const struct m0_verno *vn0,
				       const struct m0_verno *vn1);

/**
   Increments unit version number.

   This function pushes given fol record to the front of linked list of records
   describing unit serial history (see HLD).

   It assumes that the unit corresponds to the index-th slot in @rec's
   m0_fol_rec_desc::rd_ref[] array.

   @pre index < rec->fr_desc.rd_header.rh_obj_nr
   @pre m0_lsn_is_valid(rec->fr_desc.rd_lsn)

   @post unit->vn_lsn = rec->fr_desc.rd_lsn
   @post m0_verno_cmp(&rec->fr_desc.rd_ref[index].or_before_ver, unit) == -1
 */
M0_INTERNAL void m0_verno_inc(struct m0_verno *unit,
			      struct m0_fol_rec *rec, uint32_t index);

/** @} end of dtm group */

/* __MERO_DTM_VERNO_H__ */
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
