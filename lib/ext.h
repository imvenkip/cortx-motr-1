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

#ifndef __COLIBRI_LIB_EXT_H__
#define __COLIBRI_LIB_EXT_H__

#include "lib/types.h"
#include "lib/cdefs.h"

/**
   @defgroup ext Extent
   @{
 */

/** extent [ e_start, e_end ) */
struct c2_ext {
	c2_bindex_t e_start;
	c2_bindex_t e_end;
};

c2_bcount_t c2_ext_length(const struct c2_ext *ext);
bool        c2_ext_is_in (const struct c2_ext *ext, c2_bindex_t index);

bool c2_ext_are_overlapping(const struct c2_ext *e0, const struct c2_ext *e1);
bool c2_ext_is_partof(const struct c2_ext *super, const struct c2_ext *sub);
bool c2_ext_equal(const struct c2_ext *a, const struct c2_ext *b);
bool c2_ext_is_empty(const struct c2_ext *ext);
void c2_ext_intersection(const struct c2_ext *e0, const struct c2_ext *e1,
			 struct c2_ext *result);
/* must work correctly when minuend == difference */
void c2_ext_sub(const struct c2_ext *minuend, const struct c2_ext *subtrahend,
		struct c2_ext *difference);
/* must work correctly when sum == either of terms. */
void c2_ext_add(const struct c2_ext *term0, const struct c2_ext *term1,
		struct c2_ext *sum);

/* what about signed? */
c2_bindex_t c2_ext_cap(const struct c2_ext *ext2, c2_bindex_t val);

/** Tells if start of extent is less than end of extent. */
bool c2_ext_is_valid(const struct c2_ext *ext);

/** @} end of ext group */

/* __COLIBRI_LIB_EXT_H__ */
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
