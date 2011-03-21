/* -*- C -*- */

#include "lib/cdefs.h"
#include "lib/buf.h"

/**
   @addtogroup buf Basic buffer type
   @{
*/

void c2_buf_init(struct c2_buf *buf, void *data, uint32_t nob)
{
	buf->b_addr = data;
	buf->b_nob  = nob;
}
C2_EXPORTED(c2_buf_init);


/** @} end of buf group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
