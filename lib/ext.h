/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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

#pragma once

#ifndef __MERO_LIB_EXT_H__
#define __MERO_LIB_EXT_H__

#include "lib/types.h"
#include "lib/cdefs.h"

/**
   @defgroup ext Extent
   @{
 */

/** extent [ e_start, e_end ) */
struct m0_ext {
	m0_bindex_t e_start;
	m0_bindex_t e_end;
};

M0_INTERNAL m0_bcount_t m0_ext_length(const struct m0_ext *ext);
M0_INTERNAL bool m0_ext_is_in(const struct m0_ext *ext, m0_bindex_t index);

M0_INTERNAL bool m0_ext_are_overlapping(const struct m0_ext *e0,
					const struct m0_ext *e1);
M0_INTERNAL bool m0_ext_is_partof(const struct m0_ext *super,
				  const struct m0_ext *sub);
M0_INTERNAL bool m0_ext_equal(const struct m0_ext *a, const struct m0_ext *b);
M0_INTERNAL bool m0_ext_is_empty(const struct m0_ext *ext);
M0_INTERNAL void m0_ext_intersection(const struct m0_ext *e0,
				     const struct m0_ext *e1,
				     struct m0_ext *result);
/* must work correctly when minuend == difference */
M0_INTERNAL void m0_ext_sub(const struct m0_ext *minuend,
			    const struct m0_ext *subtrahend,
			    struct m0_ext *difference);
/* must work correctly when sum == either of terms. */
M0_INTERNAL void m0_ext_add(const struct m0_ext *term0,
			    const struct m0_ext *term1, struct m0_ext *sum);

/* what about signed? */
M0_INTERNAL m0_bindex_t m0_ext_cap(const struct m0_ext *ext2, m0_bindex_t val);

/** Tells if start of extent is less than end of extent. */
M0_INTERNAL bool m0_ext_is_valid(const struct m0_ext *ext);

/** @} end of ext group */

/* __MERO_LIB_EXT_H__ */
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
