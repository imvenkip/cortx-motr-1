/* -*- C -*- */

#ifndef __COLIBRI_STOB_AD_INTERNAL_H__
#define __COLIBRI_STOB_AD_INTERNAL_H__

/**
   @defgroup stobad

   <b>Storage object type based on Allocation Data (AD) stored in a
   data-base.</b>

   @{
 */

#include "db/extmap.h"
#include "stob/stob.h"

struct c2_dbenv;
struct c2_dtx;

extern struct c2_stob_type ad_stob_type;

struct ad_balloc_ops;
struct ad_balloc {
	const struct ad_balloc_ops *ab_ops;
};

struct ad_balloc_ops {
	int  (*bo_init)(struct ad_balloc *ballroom, struct c2_dbenv *db);
	void (*bo_fini)(struct ad_balloc *ballroom);
	int  (*bo_alloc)(struct ad_balloc *ballroom, struct c2_dtx *tx,
			 c2_bcount_t count, struct c2_ext *out);
	int  (*bo_free)(struct ad_balloc *ballroom, struct c2_dtx *tx,
			struct c2_ext *ext);
};


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
