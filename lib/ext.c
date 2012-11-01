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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 08/19/2010
 */

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
C2_EXPORTED(c2_ext_length);

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

bool c2_ext_equal(const struct c2_ext *a, const struct c2_ext *b)
{
	return a->e_start == b->e_start && a->e_end == b->e_end;
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
C2_EXPORTED(c2_ext_intersection);

bool c2_ext_is_valid(const struct c2_ext *ext)
{
        return ext->e_end > ext->e_start;
}
C2_EXPORTED(c2_ext_is_valid);

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
