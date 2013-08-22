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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/24/2010
 */

#pragma once

#ifndef __MERO_STOB_AD_INTERNAL_H__
#define __MERO_STOB_AD_INTERNAL_H__

/**
   @defgroup stobad Storage objects with extent maps.

   <b>Storage object type based on Allocation Data (AD) stored in a
   data-base.</b>

   AD storage object type (m0_ad_stob_type) manages collections of storage
   objects with in an underlying storage object. The underlying storage object
   is specified per-domain by a call to m0_ad_stob_setup() function.

   m0_ad_stob_type uses data-base (also specified as a parameter to
   m0_ad_stob_setup()) to store extent map (m0_emap) which keeps track of
   mapping between logical offsets in AD stobs and physical offsets within
   underlying stob.

   @{
 */

#include "stob/stob.h"

struct m0_ext;
struct m0_be_seg;
struct m0_dtx;

extern struct m0_stob_type m0_ad_stob_type;

struct m0_ad_balloc_ops;

/**
   Simple block allocator interface used by ad code to manage "free space" in
   the underlying storage object.

   @see mock_balloc
 */
struct m0_ad_balloc {
	const struct m0_ad_balloc_ops *ab_ops;
};

struct m0_ad_balloc_ops {
	/** Initializes this balloc instance, creating its persistent state, if
	    necessary. This also destroys allocated struct m0_balloc instance
	    on failure.

	    @param block size shift in bytes, similarly to
	    m0_stob_op::sop_block_shift().
	    @param container_size Total size of the container in bytes
	    @param  blocks_per_group # of blocks per group
	    @param res_groups # of reserved groups
	 */
	int  (*bo_init)(struct m0_ad_balloc *ballroom, struct m0_be_seg *db,
			struct m0_sm_group *grp,
			uint32_t bshift, m0_bcount_t container_size,
			m0_bcount_t blocks_per_group, m0_bcount_t res_groups);
	/** Finalises and destroys struct m0_balloc instance. */
	void (*bo_fini)(struct m0_ad_balloc *ballroom);
	/** Allocates count of blocks. On success, allocated extent, also
	    measured in blocks, is returned in out parameter. */
	int  (*bo_alloc)(struct m0_ad_balloc *ballroom, struct m0_dtx *dtx,
			 m0_bcount_t count, struct m0_ext *out);
	/** Free space (possibly a sub-extent of an extent allocated
	    earlier). */
	int  (*bo_free)(struct m0_ad_balloc *ballroom, struct m0_dtx *dtx,
			struct m0_ext *ext);
	void (*bo_alloc_credit)(const struct m0_ad_balloc *ballroom, int nr,
				      struct m0_be_tx_credit *accum);
	void (*bo_free_credit)(const struct m0_ad_balloc *ballroom, int nr,
				     struct m0_be_tx_credit *accum);
};

/**
   Setup an AD storage domain.

   @param adom - AD type stob domain;
   @param be - a back-end environment where domain stores its meta-data
   (extent map);
   @param bstore - an underlying storage object, where domain stores its
   objects;
   @param ballroom - a byte allocator;
   @param container_size - Container size for balloc;
   @param bshift - Block shift value;
   @param blocks_per_group - Number of blocks per group;
   @param res_groups - Number of reserved groups.
 */
M0_INTERNAL int m0_ad_stob_setup(struct m0_stob_domain *dom,
				 struct m0_be_seg *be_seg,
				 struct m0_sm_group *grp,
				 struct m0_stob *bstore,
				 struct m0_ad_balloc *ballroom,
				 m0_bcount_t container_size, uint32_t bshift,
				 m0_bcount_t blocks_per_group,
				 m0_bcount_t res_groups);

M0_INTERNAL int m0_ad_stobs_init(void);
M0_INTERNAL void m0_ad_stobs_fini(void);

/**
   @param stob - linux backend stob object for AD.
 */
M0_INTERNAL int m0_ad_stob_domain_locate(const char *domain_name,
				         struct m0_stob_domain **dom,
				         struct m0_stob *stob);

/** @} end group stobad */

/* __MERO_STOB_AD_INTERNAL_H__ */
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
