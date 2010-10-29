/* -*- C -*- */

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

/** count of bytes (in extent, IO operation, etc.) */
typedef uint64_t c2_bcount_t;
/** an index (offset) in a linear name-space (e.g., in a file, storage object,
    storage device, memory buffer) measured in bytes */
typedef uint64_t c2_bindex_t;

enum {
	C2_BCOUNT_MAX = 0xffffffffffffffff,
	C2_BINDEX_MAX = C2_BCOUNT_MAX - 1
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
