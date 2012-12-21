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
 * Original creation date: 05/06/2010
 */

#pragma once

#ifndef __MERO_LIB_ASSERT_H__
#define __MERO_LIB_ASSERT_H__

/**
   @defgroup assert Assertions, pre-conditions, post-conditions, invariants.

   @{
*/

#ifndef __KERNEL__
#include "user_space/assert.h"
#else
#include "linux_kernel/assert.h"
#endif

#ifdef ENABLE_EXPENSIVE_CHECKS
#define M0_ASSERT_EX_ON (1)
#else
#define M0_ASSERT_EX_ON (0)
#endif

/**
 * The same as M0_ASSERT macro, but this version is disabled (optimized out) if
 * ENABLE_EXPENSIVE_CHECKS macro is defined, which is controlled by configure
 * option --disable-expensive-checks.
 */
#define M0_ASSERT_EX(cond)		\
({					\
	if (M0_ASSERT_EX_ON)		\
		M0_ASSERT(cond);	\
})

/**
   A macro to check that a function pre-condition holds. M0_PRE() is
   functionally equivalent to M0_ASSERT().

   @see M0_POST()
 */
#define M0_PRE(cond) M0_ASSERT(cond)

#define M0_PRE_EX(cond) M0_ASSERT_EX(cond)

/**
   A macro to check that a function post-condition holds. M0_POST() is
   functionally equivalent to M0_ASSERT().

   @see M0_PRE()
 */
#define M0_POST(cond) M0_ASSERT(cond)

#define M0_POST_EX(cond) M0_ASSERT_EX(cond)

/**
 * A macro to check that invariant is held.
 */
#define M0_INVARIANT_EX(cond) M0_ASSERT_EX(cond)

/**
 * A macro, which intended to wrap some expensive checks, like invariant calls
 * in expressions. It statically expands to true if ENABLE_EXPENSIVE_CHECKS is
 * not defined, which allows compiler to optimize out evaluation of the argument
 * of this macro.
 */
#define M0_CHECK_EX(cond) (!M0_ASSERT_EX_ON || (cond))

/**
   A macro to assert that compile-time condition is true. Condition must be a
   constant expression (as defined in the section 6.6 of ISO/IEC
   9899). M0_CASSERT() can be used anywhere where a statement can be.

   @see M0_BASSERT()
 */
#define M0_CASSERT(cond) do { switch (1) {case 0: case !!(cond): ;} } while (0)

/**
   A macro to assert that compile-time condition is true. Condition must be a
   constant expression. M0_BASSERT() can be used anywhere where a declaration
   can be.

   @see M0_CASSERT()
 */

#define M0_BASSERT(cond)			\
	extern char __static_assertion[(cond) ? 1 : -1]

/**
   A macro indicating that computation reached an invalid state.
 */
#define M0_IMPOSSIBLE(msg) M0_ASSERT("Impossible: " msg == NULL)

/** @} end of assert group */

/* __MERO_LIB_ASSERT_H__ */
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
