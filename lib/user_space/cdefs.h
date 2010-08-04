/* -*- C -*- */

#ifndef __COLIBRI_LIB_USER_SPACE_CDEFS_H_
#define __COLIBRI_LIB_USER_SPACE_CDEFS_H_

#ifndef offsetof
#define offsetof(typ,memb) ((unsigned long)((char *)&(((typ *)0)->memb)))
#endif

#ifndef container_of
#define container_of(ptr, type, member) \
        ((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))
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

/* __COLIBRI_LIB_USER_SPACE_CDEFS_H_ */
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
