/* -*- C -*- */

#ifndef __COLIBRI_LIB_THREAD_H__
#define __COLIBRI_LIB_THREAD_H__

#include <sys/types.h>

#include "cdefs.h"

/**
   @defgroup thread Thread
   @{
 */

struct c2_thread {
	int t_x;
};

int  c2_thread_init(struct c2_thread *q, void (*func)(void *), void *arg);
void c2_thread_fini(struct c2_thread *q);
void c2_thread_join(struct c2_thread *q);

/** @} end of queue group */

/* __COLIBRI_LIB_THREAD_H__ */
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
