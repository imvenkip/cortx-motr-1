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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 12/02/2010
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/memory.h"
#include "lib/cdefs.h"
#include "lib/errno.h"
#include "lib/misc.h"
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

void c2_buf_free(struct c2_buf *buf)
{
	c2_free(buf->b_addr);
	buf->b_addr = NULL;
	buf->b_nob = 0;
}

bool c2_buf_eq(const struct c2_buf *x, const struct c2_buf *y)
{
	return x->b_nob == y->b_nob &&
		memcmp(x->b_addr, y->b_addr, x->b_nob) == 0;
}

int c2_buf_copy(struct c2_buf *dest, const struct c2_buf *src)
{
	C2_PRE(dest->b_nob == 0 && dest->b_addr == NULL);

	C2_ALLOC_ARR(dest->b_addr, src->b_nob);
	if (dest->b_addr == NULL)
		return -ENOMEM;
	dest->b_nob = src->b_nob;
	memcpy(dest->b_addr, src->b_addr, src->b_nob);

	C2_POST(c2_buf_eq(dest, src));
	return 0;
}


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
