/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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

#ifndef __COLIBRI_STOB_AD_INTERNAL_H__
#define __COLIBRI_STOB_AD_INTERNAL_H__

/**
   @defgroup stobad Storage objects with extent maps.

   <b>Storage object type based on Allocation Data (AD) stored in a
   data-base.</b>

   AD storage object type (c2_ad_stob_type) manages collections of storage
   objects with in an underlying storage object. The underlying storage object
   is specified per-domain by a call to c2_ad_stob_setup() function.

   c2_ad_stob_type uses data-base (also specified as a parameter to
   c2_ad_stob_setup()) to store extent map (c2_emap) which keeps track of
   mapping between logical offsets in AD stobs and physical offsets within
   underlying stob.

   @{
 */

#include "stob/stob.h"


struct c2_ext;
struct c2_dbenv;
struct c2_dtx;

extern struct c2_stob_type c2_ad_stob_type;

struct c2_ad_balloc_ops;

/**
   Simple block allocator interface used by ad code to manage "free space" in
   the underlying storage object.

   @see mock_balloc
 */
struct c2_ad_balloc {
	const struct c2_ad_balloc_ops *ab_ops;
};

struct c2_ad_balloc_ops {
	/** Initializes this balloc instance, creating its persistent state, if
	    necessary. This also destroys allocated struct c2_balloc instance
	    on failure.

	    @param block size shift in bytes, similarly to
	    c2_stob_op::sop_block_shift().
	    @param container_size Total size of the container in bytes
	    @param  blocks_per_group # of blocks per group
	    @param res_groups # of reserved groups
	 */
	int  (*bo_init)(struct c2_ad_balloc *ballroom, struct c2_dbenv *db,
			uint32_t bshift, c2_bcount_t container_size,
			c2_bcount_t blocks_per_group, c2_bcount_t res_groups);
	/** Finalises and destroys struct c2_balloc instance. */
	void (*bo_fini)(struct c2_ad_balloc *ballroom);
	/** Allocates count of blocks. On success, allocated extent, also
	    measured in blocks, is returned in out parameter. */
	int  (*bo_alloc)(struct c2_ad_balloc *ballroom, struct c2_dtx *dtx,
			 c2_bcount_t count, struct c2_ext *out);
	/** Free space (possibly a sub-extent of an extent allocated
	    earlier). */
	int  (*bo_free)(struct c2_ad_balloc *ballroom, struct c2_dtx *dtx,
			struct c2_ext *ext);
};

/**
   Setup an AD storage domain.

   @param adom - AD type stob domain;
   @param dbenv - a data-base environment where domain stores its meta-data
   (extent map);
   @param bstore - an underlying storage object, where domain stores its
   objects;
   @param ballroom - a byte allocator;
   @param container_size - Container size for balloc;
   @param bshift - Block shift value;
   @param blocks_per_group - Number of blocks per group;
   @param res_groups - Number of reserved groups.
 */
int  c2_ad_stob_setup(struct c2_stob_domain *adom, struct c2_dbenv *dbenv,
		      struct c2_stob *bstore, struct c2_ad_balloc *ballroom,
		      c2_bindex_t container_size, c2_bcount_t bshift,
		      c2_bcount_t blocks_per_group, c2_bcount_t res_groups);

int  c2_ad_stobs_init(void);
void c2_ad_stobs_fini(void);

/** @} end group stobad */

/* __COLIBRI_STOB_AD_INTERNAL_H__ */
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
