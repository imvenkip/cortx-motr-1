/* -*- C -*- */
#ifndef __COLIBRI_LIB_CDEFS_H_

#define __COLIBRI_LIB_CDEFS_H_

#ifndef __KERNEL__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef offsetof
#define offsetof(typ,memb) ((unsigned long)((char *)&(((typ *)0)->memb)))
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
        ((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))
#endif

#include <limits.h>

#if LONG_MAX == 9223372036854775807L
#define BITS_PER_LONG	64
#elif LONG_MAX == 2147483647L
#define BITS_PER_LONG	32
#elif LONG_MAX == 65535
#define BITS_PER_LONG	16
#else
#error BITS per LONG not defined!
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

/**
 * size of static array
 */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) ((sizeof (a)) / (sizeof (a)[0] ))
#endif

#define EXPORT_SYMBOL(s) 

#else /* __KERNEL__ */

#include <linux/types.h>
#include <linux/kernel.h>

#endif /* __KERNEL__ */

#define ergo(a, b) (!(a) || (b))
#define equi(a, b) (!(a) == !(b))

extern void __dummy_function(void);

/**
   A macro used with if-statements without `else' clause to assure proper
   coverage analysis.
 */
#define AND_NOTHING_ELSE else __dummy_function();

struct c2_uint128 {
	uint64_t u_hi;
	uint64_t u_lo;
};

bool c2_uint128_eq (const struct c2_uint128 *u0, const struct c2_uint128 *u1);
int  c2_uint128_cmp(const struct c2_uint128 *u0, const struct c2_uint128 *u1);
void c2_uint128_init(struct c2_uint128 *u128, const char *magic);

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
