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

#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "lib/buf.h"

/**
   @addtogroup buf Basic buffer type
   @{
*/

M0_INTERNAL void m0_buf_init(struct m0_buf *buf, void *data, uint32_t nob)
{
	buf->b_addr = data;
	buf->b_nob  = nob;
}

M0_INTERNAL void m0_buf_free(struct m0_buf *buf)
{
	m0_free0(&buf->b_addr);
	buf->b_nob = 0;
}

M0_INTERNAL bool m0_buf_eq(const struct m0_buf *x, const struct m0_buf *y)
{
	return x->b_nob == y->b_nob &&
		memcmp(x->b_addr, y->b_addr, x->b_nob) == 0;
}

M0_INTERNAL int m0_buf_copy(struct m0_buf *dest, const struct m0_buf *src)
{
	M0_PRE(dest->b_nob == 0 && dest->b_addr == NULL);
	M0_PRE(src->b_nob > 0 && src->b_addr != NULL);

	M0_ALLOC_ARR(dest->b_addr, src->b_nob);
	if (dest->b_addr == NULL)
		return -ENOMEM;
	dest->b_nob = src->b_nob;
	memcpy(dest->b_addr, src->b_addr, src->b_nob);

	M0_POST(m0_buf_eq(dest, src));
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
