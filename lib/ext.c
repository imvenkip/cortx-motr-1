/* -*- C -*- */

#include "lib/arith.h"        /* max_check, min_check */
#include "lib/ext.h"

/**
   @addtogroup ext
   @{
 */

c2_bcount_t c2_ext_length(const struct c2_ext *ext)
{
	return ext->e_end - ext->e_start;
}

bool c2_ext_is_in(const struct c2_ext *ext, c2_bindex_t index)
{
	return ext->e_start <= index && index < ext->e_end;
}

bool c2_ext_is_partof(const struct c2_ext *super, const struct c2_ext *sub)
{
	return 
		c2_ext_is_in(super, sub->e_start) && 
		sub->e_end <= super->e_end;
}

bool c2_ext_is_empty(const struct c2_ext *ext)
{
	return ext->e_end <= ext->e_start;
}

void c2_ext_intersection(const struct c2_ext *e0, const struct c2_ext *e1,
			 struct c2_ext *result)
{
	result->e_start = max_check(e0->e_start, e1->e_start);
	result->e_end   = min_check(e0->e_end,   e1->e_end);
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
