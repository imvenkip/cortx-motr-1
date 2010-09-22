/* -*- C -*- */

#include <string.h>      /* memcmp */

#include "fid/fid.h"

/**
   @addtogroup fid

   @{
 */

bool c2_fid_is_valid(const struct c2_fid *fid)
{
	return true;
}

bool c2_fid_eq(const struct c2_fid *fid0, const struct c2_fid *fid1)
{
	return memcmp(fid0, fid1, sizeof *fid0) == 0;
}

/** @} end of fid group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
