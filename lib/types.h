/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/14/2010
 */

#pragma once

#ifndef __MERO_LIB_TYPES_H__
#define __MERO_LIB_TYPES_H__

#ifdef __KERNEL__
#include "lib/linux_kernel/types.h"
#else
#include "lib/user_space/types.h"
#endif
#include "xcode/xcode_attr.h"

struct m0_uint128 {
	uint64_t u_hi;
	uint64_t u_lo;
} M0_XCA_RECORD;

#define M0_UINT128(hi, lo) (struct m0_uint128) { .u_hi = (hi), .u_lo = (lo) }

M0_INTERNAL bool m0_uint128_eq(const struct m0_uint128 *u0,
			       const struct m0_uint128 *u1);
M0_INTERNAL int m0_uint128_cmp(const struct m0_uint128 *u0,
			       const struct m0_uint128 *u1);
M0_INTERNAL void m0_uint128_init(struct m0_uint128 *u128, const char *magic);
/** res = a + b; */
M0_INTERNAL void m0_uint128_add(struct m0_uint128 *res,
				const struct m0_uint128 a,
				const struct m0_uint128 b);
/** res = a * b; */
M0_INTERNAL void m0_uint128_mul64(struct m0_uint128 *res, uint64_t a,
				  uint64_t b);

/** count of bytes (in extent, IO operation, etc.) */
typedef uint64_t m0_bcount_t;
/** an index (offset) in a linear name-space (e.g., in a file, storage object,
    storage device, memory buffer) measured in bytes */
typedef uint64_t m0_bindex_t;

enum {
	M0_BCOUNT_MAX = 0xffffffffffffffff,
	M0_BINDEX_MAX = M0_BCOUNT_MAX - 1,
	M0_BSIGNED_MAX = 0x7fffffffffffffff
};


/* __MERO_LIB_TYPES_H__ */
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
