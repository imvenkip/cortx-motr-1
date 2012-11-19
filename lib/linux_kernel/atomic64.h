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
 * Original author: Huang Hua <hua_huang@xyratex.com>
 * Original creation date: 12/02/2010
 */

#pragma once

#ifndef __COLIBRI_LIB_LINUX_KERNEL_ATOMIC64_H__
#define __COLIBRI_LIB_LINUX_KERNEL_ATOMIC64_H__

#include "lib/types.h"
#include "lib/cdefs.h"
#include "lib/assert.h"

/**
   @addtogroup atomic

   Implementation of atomic operations for Linux user space uses x86_64 assembly
   language instructions (with gcc syntax). "Lock" prefix is used
   everywhere---no optimisation for non-SMP configurations in present.
 */

struct c2_atomic64 {
	atomic64_t a_value;
};

static inline void c2_atomic64_set(struct c2_atomic64 *a, int64_t num)
{
	C2_CASSERT(sizeof a->a_value == sizeof num);

	atomic64_set(&a->a_value, num);
}

static inline int64_t c2_atomic64_get(const struct c2_atomic64 *a)
{
	return	atomic64_read(&a->a_value);
}

static inline void c2_atomic64_inc(struct c2_atomic64 *a)
{
	atomic64_inc(&a->a_value);
}

static inline void c2_atomic64_dec(struct c2_atomic64 *a)
{
	atomic64_dec(&a->a_value);
}

static inline void c2_atomic64_add(struct c2_atomic64 *a, int64_t num)
{
	atomic64_add(num, &a->a_value);
}

static inline void c2_atomic64_sub(struct c2_atomic64 *a, int64_t num)
{
	atomic64_sub(num, &a->a_value);
}


static inline int64_t c2_atomic64_add_return(struct c2_atomic64 *a,
						  int64_t delta)
{
	return atomic64_add_return(delta, &a->a_value);
}

static inline int64_t c2_atomic64_sub_return(struct c2_atomic64 *a,
						  int64_t delta)
{
	return atomic64_sub_return(delta, &a->a_value);
}

static inline bool c2_atomic64_inc_and_test(struct c2_atomic64 *a)
{
	return atomic64_inc_and_test(&a->a_value);
}

static inline bool c2_atomic64_dec_and_test(struct c2_atomic64 *a)
{
	return atomic64_dec_and_test(&a->a_value);
}

static inline bool c2_atomic64_cas(int64_t * loc, int64_t old, int64_t new)
{
	return cmpxchg64(loc, old, new) == old;
}

/** @} end of atomic group */

/* __COLIBRI_LIB_LINUX_KERNEL_ATOMIC64_H__ */
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
