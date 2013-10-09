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

#include <stddef.h>   /* NULL */
#include <stdarg.h>   /* va_list */

/**
   @defgroup assert Assertions, pre-conditions, post-conditions, invariants.

   @{
*/

/* this should be defined before target-specific assert.h is included */
#ifdef M0_NDEBUG
#define M0_ASSERT_OFF (1)
#else
#define M0_ASSERT_OFF (0)
#endif

#ifdef ENABLE_EXPENSIVE_CHECKS
#define M0_ASSERT_EX_ON (1)
#else
#define M0_ASSERT_EX_ON (0)
#endif

/*
 * likely() and unlikely() are defined here rather than in lib/misc.h to avoid
 * circular dependency.
 */
#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

/**
 * Panic context
 */
struct m0_panic_ctx {
	/**
	 * Panic message, usually it's a failed condition, which "triggers"
	 * panic.
	 */
	const char                  *pc_expr;
	/** Name of a function, which calls m0_panic(), i.e. __func__ */
	const char                  *pc_func;
	/** Name of a file, i.e. __FILE__ */
	const char                  *pc_file;
	/** Line number, i.e. __LINE__ */
	int                          pc_lineno;
	/**
	 * Additional informational message with printf(3) like formatting,
	 * which will be displayed after the failed condition and can be used as
	 * an explanation of why condition has failed
	 */
	const char                  *pc_fmt;
};

/**
 * Display panic message and abort program execution.
 *
 * @param ctx     panic context
 * @param ...     arguments for printf format string m0_panic_ctx::pc_fmt
 */
void m0_panic(const struct m0_panic_ctx *ctx, ...)
	__attribute__((noreturn));

M0_INTERNAL void m0_arch_panic(const struct m0_panic_ctx *ctx, va_list ap)
	__attribute__((noreturn));

void m0_backtrace(void);
M0_INTERNAL void m0_arch_backtrace(void);

/**
 * Check printf format string against parameters.
 *
 * This function does nothing except checking that the format string matches the
 * rest of arguments and producing a compilation warning in case it doesn't. It
 * is handy in macros which accept printf-like parameters with a format string.
 *
 * For example usage, refer to M0_TRACE_POINT() macro
 */
__attribute__ ((format (printf, 1, 2))) static inline void
printf_check(const char *fmt, ...)
{}

/**
 * The same as M0_ASSERT macro, but this version allows to specify additional
 * informational message with printf(3) like formatting, which will be displayed
 * after the failed condition and can be used as an explanation of why condition
 * has failed.
 *
 * Since our headers are processed by gccxml, which is essentially a C++
 * compiler, we can't use C99 compound literals and designated initializers to
 * construct m0_panic_ctx, so we use positional initializers and their order
 * should match the fields declaration order in m0_panic_ctx structure.
 */
#define M0_ASSERT_INFO(cond, fmt, ...)					\
({									\
	static const struct m0_panic_ctx __pctx = {			\
		#cond, __func__, __FILE__, __LINE__, (fmt)		\
	};								\
	printf_check(fmt, ##__VA_ARGS__);				\
	(M0_ASSERT_OFF || likely(cond) ? (void)0 : m0_panic(&__pctx, ##__VA_ARGS__)); \
})

/**
 * A macro to assert that a condition is true. If condition is true, M0_ASSERT()
 * does nothing. Otherwise it emits a diagnostics message and terminates the
 * system. The message and the termination method are platform dependent.
 */
#define M0_ASSERT(cond)  M0_ASSERT_INFO((cond), NULL)

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
 * A hybrid of M0_ASSERT_INFO() and M0_ASSERT_EX().
 */
#define M0_ASSERT_INFO_EX(cond, fmt, ...)			\
({								\
	if (M0_ASSERT_EX_ON)					\
		M0_ASSERT_INFO((cond), (fmt), ##__VA_ARGS__);	\
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

#define M0_BASSERT(cond) extern char __static_assertion[(cond) ? 1 : -1]

/**
   A macro indicating that computation reached an invalid state.
 */
#define M0_IMPOSSIBLE(msg) M0_ASSERT("Impossible: " msg == NULL)

/**
   Location where _0C() macro stores the name of failed asserted expression.
 */
M0_EXTERN const char *m0_failed_condition;

/**
   Called by _0C() when invariant conjunct fails.

   Useful thing to put a breakpoint at.
 */
M0_INTERNAL void m0__assertion_hook(void);
/**
   A macro to remember failed invariant conjunct.

   This macro is used like the following:

@code
bool foo_invariant(const struct foo *f)
{
	return _0C(f != NULL) && _0C(f->f_ref > 0) &&
		m0_tl_forall(bar, s, &foo->f_list, _0C(b->b_parent == f) &&
		                  _0C(b->b_nr < f->f_nr));
}
@endcode

   If during invocation of foo_invariant() one of invariant conjuncts evaluates
   to false, the string representing this conjunct is stored in
   m0_failed_condition and printed by m0_panic(). This simplifies debugging.

   @note This macro expressly and deliberately violates "M0_" prefix requirement
   to reduce verbosity.

   @note This compiles to "exp" if M0_ASSERT_OFF is true.
 */
#define _0C(exp)				\
({						\
	bool __exp = (exp);			\
	if (!M0_ASSERT_OFF && !__exp) {		\
		m0_failed_condition = #exp;	\
		m0__assertion_hook();		\
	}					\
	__exp;					\
})

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
