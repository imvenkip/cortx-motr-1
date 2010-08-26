/* -*- C -*- */

#ifndef __COLIBRI_STOB_AD_INTERNAL_H__
#define __COLIBRI_STOB_AD_INTERNAL_H__

/**
   @defgroup stobad

   <b>Storage object type based on Allocation Data (AD) stored in a
   data-base.</b>

   AD storage object type (ad_stob_type) manages collections of storage objects
   with in an underlying storage object. The underlying storage object is
   specified per-domain by a call to ad_setup() function.

   ad_stob_type uses data-base (also specified as a parameter to ad_setup()) to
   store extent map (c2_emap) which keeps track of mapping between logical
   offsets in AD stobs and physical offsets within underlying stob.

   @{
 */

#include "db/extmap.h"
#include "stob/stob.h"

struct c2_dbenv;
struct c2_dtx;

extern struct c2_stob_type ad_stob_type;

struct ad_balloc_ops;

/**
   Simple block allocator interface used by ad code to manage "free space" in
   the underlying storage object.

   @see mock_balloc
 */
struct ad_balloc {
	const struct ad_balloc_ops *ab_ops;
};

struct ad_balloc_ops {
	/** Initializes this balloc instance, creating its persistent state, if
	    necessary. */
	int  (*bo_init)(struct ad_balloc *ballroom, struct c2_dbenv *db);
	void (*bo_fini)(struct ad_balloc *ballroom);
	/** Allocated count bytes. On success, allocated extent, also measured
	    in bytes, is returned in out parameter. */
	int  (*bo_alloc)(struct ad_balloc *ballroom, struct c2_dtx *tx,
			 c2_bcount_t count, struct c2_ext *out);
	/** Free space (possibly a sub-extent of an extent allocated earlier. */
	int  (*bo_free)(struct ad_balloc *ballroom, struct c2_dtx *tx,
			struct c2_ext *ext);
};

/**
   Setup an AD storage domain.

   @param dbenv - a data-base environment where domain stores its meta-data
   (extent map);

   @param bstore - an underlying storage object, where domain stores its
   objects;

   @param ballroom - a byte allocator.
 */
int  ad_setup(struct c2_stob_domain *adom, struct c2_dbenv *dbenv,
	      struct c2_stob *bstore, struct ad_balloc *ballroom);

int  ad_stobs_init(void);
void ad_stobs_fini(void);

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
