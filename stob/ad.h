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

struct c2_space_allocator;
struct c2_dbenv;

extern struct c2_stob_type ad_stob_type;

int  ad_setup(struct c2_stob_domain *adom, struct c2_dbenv *dbenv,
	      struct c2_stob *bstore, struct c2_space_allocator *balloc);

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
