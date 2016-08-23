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

#include "be/extmap.h"		/* m0_be_emap */
#include "fid/fid.h"		/* m0_fid */
#include "lib/types.h"		/* m0_bcount_t */
#include "stob/domain.h"	/* m0_stob_domain */
#include "stob/io.h"		/* m0_stob_io */
#include "stob/stob.h"		/* m0_stob */

/**
   @defgroup stobad Storage objects with extent maps.

   <b>Storage object type based on Allocation Data (AD) stored in a
   data-base.</b>

   AD storage object type (m0_stob_ad_type) manages collections of storage
   objects with in an underlying storage object. The underlying storage object
   is specified per-domain by a call to m0_ad_stob_setup() function.

   m0_stob_ad_type uses data-base (also specified as a parameter to
   m0_ad_stob_setup()) to store extent map (m0_emap) which keeps track of
   mapping between logical offsets in AD stobs and physical offsets within
   underlying stob.

   @{
 */

struct m0_ad_balloc;
struct m0_ad_balloc_ops;
struct m0_be_seg;

/**
   Simple block allocator interface used by ad code to manage "free space" in
   the underlying storage object.

   @see mock_balloc
 */
struct m0_ad_balloc {
	const struct m0_ad_balloc_ops *ab_ops;
};

struct m0_ad_balloc_ops {
	/** Initialises this balloc instance, creating its persistent state, if
	    necessary. This also destroys allocated struct m0_balloc instance
	    on failure.

	    @param block size shift in bytes, similarly to m0_stob_block_shift()
	    @param container_size Total size of the container in bytes
	    @param  blocks_per_group # of blocks per group
	 */
	int  (*bo_init)(struct m0_ad_balloc *ballroom, struct m0_be_seg *db,
			uint32_t bshift, m0_bcount_t container_size,
			m0_bcount_t blocks_per_group);
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

struct m0_stob_ad_domain {
	struct m0_format_header sad_header;
	struct m0_stob_domain   sad_base;
	struct m0_stob         *sad_bstore;
	struct m0_stob_id       sad_bstore_id;
	struct m0_be_emap       sad_adata;
	struct m0_ad_balloc    *sad_ballroom;
	m0_bcount_t             sad_container_size;
	uint32_t                sad_bshift;
	int32_t                 sad_babshift;
	m0_bcount_t             sad_blocks_per_group;
	struct m0_be_seg       *sad_be_seg;
	char                    sad_path[MAXPATHLEN];
	bool                    sad_overwrite;
	char                    sad_pad[7];
	struct m0_format_footer sad_footer;
};
M0_BASSERT(sizeof(M0_FIELD_VALUE(struct m0_stob_ad_domain, sad_path)) % 8 == 0);
M0_BASSERT(sizeof(bool) == 1);

/**
   Types of allocation extents.

   Values of this enum are stored as "physical extent start" in allocation
   extents.
 */
enum stob_ad_allocation_extent_type {
	/**
	    Minimal "special" extent type. All values less than this are valid
	    start values of normal allocated extents.
	 */
	AET_MIN = M0_BINDEX_MAX - (1ULL << 32),
	/**
	   This value is used to tag a hole in the storage object.
	 */
	AET_HOLE
};

/**
 * Minimilistic ad stob iterator.
 * This saves the last segment that was accessed during the stob operation.
 * @note: Currently used only for stob delete operation.
 */
struct m0_stob_ad_op_it {
	struct m0_be_emap_seg   oc_seg_last;
};

struct m0_stob_ad {
	struct m0_stob          ad_stob;
	struct m0_stob_ad_op_it ad_op_it;
};

struct m0_stob_ad_io {
	struct m0_stob_io *ai_fore;
	struct m0_stob_io  ai_back;
	struct m0_clink    ai_clink;
};

extern const struct m0_stob_type m0_stob_ad_type;

static inline struct m0_stob_ad_domain *
stob_ad_domain2ad(const struct m0_stob_domain *dom)
{
	return container_of(dom, struct m0_stob_ad_domain, sad_base);
}

M0_INTERNAL bool m0_stob_ad_domain__invariant(struct m0_stob_ad_domain *adom);

M0_INTERNAL void m0_stob_ad_cfg_make(char **str,
				     const struct m0_be_seg *seg,
				     const struct m0_stob_id *bstore_id,
				     const m0_bcount_t size);
M0_INTERNAL int stob_ad_cursor(struct m0_stob_ad_domain *adom,
			       struct m0_stob *obj,
			       uint64_t offset,
			       struct m0_be_emap_cursor *it);


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
