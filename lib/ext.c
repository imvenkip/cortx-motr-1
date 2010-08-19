/* -*- C -*- */

#include "lib/ext.h"

/**
   @addtogroup ext
   @{
 */

c2_bcount_t c2_ext_length(const struct c2_ext *ext)
{
	return ext->e_end - ext->e_start;
}

/** @} end of ext group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
