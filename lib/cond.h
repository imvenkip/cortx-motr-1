/* -*- C -*- */

#ifndef __COLIBRI_LIB_COND_H__
#define __COLIBRI_LIB_COND_H__

#include "chan.h"

struct c2_mutex;

/**
   @defgroup cond Conditional variable.

   @{
*/

struct c2_cond {
	struct c2_chan c_chan;
};

void c2_cond_init(struct c2_cond *cond);
void c2_cond_fini(struct c2_cond *cond);

void c2_cond_wait(struct c2_cond *cond, struct c2_mutex *mutex);

void c2_cond_signal(struct c2_cond *cond);
void c2_cond_broadcast(struct c2_cond *cond);

/** @} end of cond group */

/* __COLIBRI_LIB_COND_H__ */
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
