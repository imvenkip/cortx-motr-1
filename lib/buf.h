/* -*- C -*- */

#ifndef __COLIBRI_BUF_H__
#define __COLIBRI_BUF_H__

#include "lib/types.h"
#include "lib/cdefs.h"

/**
   @defgroup buf Basic buffer type
   @{
*/

struct c2_buf {
	void       *b_addr;
	c2_bcount_t b_nob;
};

void c2_buf_init(struct c2_buf *buf, void *data, uint32_t nob);

/** @} end of buf group */


/* __COLIBRI_BUF_H__ */
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
