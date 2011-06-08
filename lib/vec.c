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
 * Original author: Nikita Danilov
 * Original creation date: 05/12/2010
 */

#include "lib/arith.h"     /* min3 */
#include "lib/cdefs.h"     /* NULL */
#include "lib/vec.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/misc.h"      /* C2_SET0, memcpy */
#include "lib/errno.h"

/**
   @addtogroup vec Vectors
   @{
*/

c2_bcount_t c2_vec_count(const struct c2_vec *vec)
{
	c2_bcount_t count;
	uint32_t    i;

	for (count = 0, i = 0; i < vec->v_nr; ++i) {
		/* overflow check */
		C2_ASSERT(count + vec->v_count[i] >= count);
		count += vec->v_count[i];
	}
	return count;
}

static bool c2_vec_cursor_invariant(const struct c2_vec_cursor *cur)
{
	return
		cur->vc_vec != NULL &&
		cur->vc_seg <= cur->vc_vec->v_nr &&
		ergo(cur->vc_seg < cur->vc_vec->v_nr,
		     cur->vc_offset < cur->vc_vec->v_count[cur->vc_seg]) &&
		ergo(cur->vc_seg == cur->vc_vec->v_nr,
		      cur->vc_offset == 0);
}

/**
   Skips over empty segments, restoring cursor invariant.
 */
static void c2_vec_cursor_normalize(struct c2_vec_cursor *cur)
{
	while (cur->vc_seg < cur->vc_vec->v_nr &&
	       cur->vc_vec->v_count[cur->vc_seg] == 0) {
		++cur->vc_seg;
		cur->vc_offset = 0;
	}
}

void c2_vec_cursor_init(struct c2_vec_cursor *cur, struct c2_vec *vec)
{
	cur->vc_vec    = vec;
	cur->vc_seg    = 0;
	cur->vc_offset = 0;
	c2_vec_cursor_normalize(cur);
	C2_ASSERT(c2_vec_cursor_invariant(cur));
}

bool c2_vec_cursor_move(struct c2_vec_cursor *cur, c2_bcount_t count)
{
	C2_ASSERT(c2_vec_cursor_invariant(cur));
	while (count > 0 && cur->vc_seg < cur->vc_vec->v_nr) {
		c2_bcount_t step;

		step = c2_vec_cursor_step(cur);
		if (count >= step) {
			cur->vc_seg++;
			cur->vc_offset = 0;
			count -= step;
		} else {
			cur->vc_offset += count;
			count = 0;
		}
		c2_vec_cursor_normalize(cur);
	}
	C2_ASSERT(c2_vec_cursor_invariant(cur));
	return cur->vc_seg == cur->vc_vec->v_nr;
}

c2_bcount_t c2_vec_cursor_step(const struct c2_vec_cursor *cur)
{
	C2_PRE(cur->vc_seg < cur->vc_vec->v_nr);
	C2_ASSERT(c2_vec_cursor_invariant(cur));
	return cur->vc_vec->v_count[cur->vc_seg] - cur->vc_offset;
}

int c2_bufvec_alloc(struct c2_bufvec *bufvec,
		    uint32_t          num_segs,
		    c2_bcount_t       seg_size)
{
	uint32_t i;

	C2_PRE(num_segs > 0 && seg_size > 0);
	bufvec->ov_buf = NULL;
	bufvec->ov_vec.v_nr = num_segs;
	C2_ALLOC_ARR(bufvec->ov_vec.v_count, num_segs);
	if (bufvec->ov_vec.v_count == NULL)
		goto fail;
	C2_ALLOC_ARR(bufvec->ov_buf, num_segs);
	if (bufvec->ov_buf == NULL)
		goto fail;

	for (i = 0; i < bufvec->ov_vec.v_nr; ++i) {
		bufvec->ov_buf[i] = c2_alloc(seg_size);
		if (bufvec->ov_buf[i] == NULL)
			goto fail;
		bufvec->ov_vec.v_count[i] = seg_size;
	}

	return 0;

fail:
	c2_bufvec_free(bufvec);
	return -ENOMEM;
}

void c2_bufvec_free(struct c2_bufvec *bufvec)
{
	if (bufvec != NULL) {
		if (bufvec->ov_buf != NULL) {
			uint32_t i;

			for (i = 0; i < bufvec->ov_vec.v_nr; ++i)
				c2_free(bufvec->ov_buf[i]);
			c2_free(bufvec->ov_buf);
		}
		c2_free(bufvec->ov_vec.v_count);
		C2_SET0(bufvec);
	}
}

void  c2_bufvec_cursor_init(struct c2_bufvec_cursor *cur,
			    struct c2_bufvec *bvec)
{
	C2_PRE(cur != NULL);
	C2_PRE(bvec != NULL &&
	       bvec->ov_vec.v_nr != 0 && bvec->ov_vec.v_count != NULL &&
	       bvec->ov_buf != NULL);
	c2_vec_cursor_init(&cur->bc_vc, &bvec->ov_vec);
}

bool c2_bufvec_cursor_move(struct c2_bufvec_cursor *cur, c2_bcount_t count)
{
	return c2_vec_cursor_move(&cur->bc_vc, count);
}

c2_bcount_t c2_bufvec_cursor_step(const struct c2_bufvec_cursor *cur)
{
	return c2_vec_cursor_step(&cur->bc_vc);
}

static void *bufvec_cursor_addr(struct c2_bufvec_cursor *cur)
{
	struct c2_vec_cursor *vc = &cur->bc_vc;
	struct c2_bufvec *bv = container_of(vc->vc_vec,struct c2_bufvec,ov_vec);

	C2_PRE(!c2_bufvec_cursor_move(cur, 0));
	return bv->ov_buf[vc->vc_seg] + vc->vc_offset;
}

void *c2_bufvec_cursor_addr(struct c2_bufvec_cursor *cur)
{
	C2_PRE(!c2_bufvec_cursor_move(cur, 0));
	return bufvec_cursor_addr(cur);
}

c2_bcount_t c2_bufvec_cursor_copy(struct c2_bufvec_cursor *dcur,
				  struct c2_bufvec_cursor *scur,
				  c2_bcount_t num_bytes)
{
	c2_bcount_t frag_size    = 0;
	c2_bcount_t bytes_copied = 0;
	/* bitwise OR used below to ensure both cursors get moved
	   without short-circuit logic, also why cursor move is before
	   simpler num_bytes check */
	while (!(c2_bufvec_cursor_move(dcur, frag_size) |
		 c2_bufvec_cursor_move(scur, frag_size)) &&
	       num_bytes > 0) {
		frag_size = min3(c2_bufvec_cursor_step(dcur),
				 c2_bufvec_cursor_step(scur),
				 num_bytes);
		memmove(bufvec_cursor_addr(dcur),
			bufvec_cursor_addr(scur),
			frag_size);
		num_bytes -= frag_size;
		bytes_copied += frag_size;
	}
	return bytes_copied;
}

/** @} end of vec group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
