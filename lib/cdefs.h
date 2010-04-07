/* -*- C -*- */
#ifndef __COLIBRI_LIB_CDEFS_H_

#define __COLIBRI_LIB_CDEFS_H_

#include <stdint.h>

#ifndef offsetof
#define offsetof(typ,memb) ((unsigned long)((char *)&(((typ *)0)->memb)))
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
        ((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))
#endif

/**
 * size of static array
 */
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) ((sizeof (a)) / (sizeof (a)[0] ))
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

typedef int bool;

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
