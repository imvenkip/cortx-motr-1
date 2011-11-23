/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anand Vidwansa <anand_vidwansa@xyratex.com>
 * Original creation date: 10/11/2011
 */

#include "lib/types.h"
#include "lib/assert.h"
#include "lib/vec.h"
#include "lib/cdefs.h"
#include <linux/pagemap.h> /* PAGE_CACHE_SIZE */

int c2_0vec_page_add(struct c2_0vec *zvec,
		     struct page *pg,
		     c2_bindex_t index)
{
	int		  rc;
	uint32_t	  curr_seg;
	struct c2_bufvec *bvec;
	struct c2_buf	  buf;

	buf.b_addr = page_address(pg);
	buf.b_nob = PAGE_CACHE_SIZE;

	rc = c2_0vec_cbuf_add(zvec, &buf, &index);
	if (rc == 0)
		C2_POST(curr_seg < bvec->ov_vec.v_nr);
	return rc;
}
C2_EXPORTED(c2_0vec_page_add);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
