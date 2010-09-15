/* -*- C -*- */

#ifndef __COLIBRI_FID_FID_H__
#define __COLIBRI_FID_FID_H__

/**
   @defgroup fid File identifier

   @{
 */

/* export */
struct fid;

/* import */
#include "lib/types.h"

struct c2_fid {
	uint64_t f_container;
	uint64_t f_key;
};

bool c2_fid_is_valid(const struct c2_fid *fid);
bool c2_fid_eq(const struct c2_fid *fid0, const struct c2_fid *fid1);

/** @} end of fid group */

/* __COLIBRI_FID_FID_H__ */
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
