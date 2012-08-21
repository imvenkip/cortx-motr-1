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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 08/14/2010
 */

#ifndef __COLIBRI_LIB_TYPES_H_
#define __COLIBRI_LIB_TYPES_H_

#ifdef __KERNEL__
#include "linux_kernel/types.h"
#else
#include "user_space/types.h"
#endif

struct c2_uint128 {
	uint64_t u_hi;
	uint64_t u_lo;
};

bool c2_uint128_eq (const struct c2_uint128 *u0, const struct c2_uint128 *u1);
int  c2_uint128_cmp(const struct c2_uint128 *u0, const struct c2_uint128 *u1);
void c2_uint128_init(struct c2_uint128 *u128, const char *magic);
void c2_uint128_set(struct c2_uint128 *u128, uint64_t hi, uint64_t lo);
/** u128 += v128; */
void c2_uint128_add(struct c2_uint128 *u128, const struct c2_uint128 *v128);
/** u128 = a * b; */
void c2_uint128_mul(struct c2_uint128 *u128, uint64_t a, uint64_t b);

/** count of bytes (in extent, IO operation, etc.) */
typedef uint64_t c2_bcount_t;
/** an index (offset) in a linear name-space (e.g., in a file, storage object,
    storage device, memory buffer) measured in bytes */
typedef uint64_t c2_bindex_t;

enum {
	C2_BCOUNT_MAX = 0xffffffffffffffff,
	C2_BINDEX_MAX = C2_BCOUNT_MAX - 1,
	C2_BSIGNED_MAX = 0x7fffffffffffffff
};


/* __COLIBRI_LIB_TYPES_H_ */
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
