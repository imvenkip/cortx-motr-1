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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
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
C2_EXPORTED(c2_vec_count);

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
C2_EXPORTED(c2_vec_cursor_init);

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
C2_EXPORTED(c2_vec_cursor_move);

c2_bcount_t c2_vec_cursor_step(const struct c2_vec_cursor *cur)
{
	C2_PRE(cur->vc_seg < cur->vc_vec->v_nr);
	C2_ASSERT(c2_vec_cursor_invariant(cur));
	return cur->vc_vec->v_count[cur->vc_seg] - cur->vc_offset;
}
C2_EXPORTED(c2_vec_cursor_step);

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
C2_EXPORTED(c2_bufvec_alloc);

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
C2_EXPORTED(c2_bufvec_free);

void  c2_bufvec_cursor_init(struct c2_bufvec_cursor *cur,
			    struct c2_bufvec *bvec)
{
	C2_PRE(cur != NULL);
	C2_PRE(bvec != NULL &&
	       bvec->ov_vec.v_nr != 0 && bvec->ov_vec.v_count != NULL &&
	       bvec->ov_buf != NULL);
	c2_vec_cursor_init(&cur->bc_vc, &bvec->ov_vec);
}
C2_EXPORTED(c2_bufvec_cursor_init);

bool c2_bufvec_cursor_move(struct c2_bufvec_cursor *cur, c2_bcount_t count)
{
	return c2_vec_cursor_move(&cur->bc_vc, count);
}
C2_EXPORTED(c2_bufvec_cursor_move);

c2_bcount_t c2_bufvec_cursor_step(const struct c2_bufvec_cursor *cur)
{
	return c2_vec_cursor_step(&cur->bc_vc);
}
C2_EXPORTED(c2_bufvec_cursor_step);

void *bufvec_cursor_addr(struct c2_bufvec_cursor *cur)
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
C2_EXPORTED(c2_bufvec_cursor_addr);

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
C2_EXPORTED(c2_bufvec_cursor_copy);

void c2_0vec_free(struct c2_0vec *zvec)
{
	if (zvec != NULL) {
		c2_free(zvec->z_bvec.ov_vec.v_count);
		c2_free(zvec->z_bvec.ov_buf);
		c2_free(zvec->z_indices);
	}
}
C2_EXPORTED(c2_0vec_free);

static bool zerovec_invariant(struct c2_0vec *zvec)
{
	int		  i;
	struct c2_bufvec *bvec;

	if (zvec == NULL || zvec->z_indices == NULL)
		return false;

	bvec = &zvec->z_bvec;
	if (bvec->ov_buf == NULL ||
	    bvec->ov_vec.v_count == NULL ||
	    bvec->ov_vec.v_nr == 0)
		return false;

	for (i = 0; i < bvec->ov_vec.v_nr; ++i)
		if (bvec->ov_vec.v_count[i] == 0)
			return false;

	if (zvec->z_cursor.bc_vc.vc_seg > bvec->ov_vec.v_nr)
		return false;

	return true;
}

int c2_0vec_init(struct c2_0vec *zvec,
		 const uint32_t segs_nr,
		 const c2_bcount_t seg_size)
{
	int i;

	C2_PRE(zvec != NULL);
	C2_PRE(segs_nr != 0);
	C2_PRE(seg_size != 0);

	C2_SET0(zvec);
	C2_ALLOC_ARR(zvec->z_indices, segs_nr);
	if (zvec->z_indices == NULL)
		return -ENOMEM;

	C2_ALLOC_ARR(zvec->z_bvec.ov_vec.v_count, segs_nr);
	if (zvec->z_bvec.ov_vec.v_count == NULL)
		goto failure;

	for (i = 0; i < segs_nr; ++i)
		zvec->z_bvec.ov_vec.v_count[i] = seg_size;

	zvec->z_bvec.ov_vec.v_nr = segs_nr;

	C2_ALLOC_ARR(zvec->z_bvec.ov_buf, segs_nr);
	if (zvec->z_bvec.ov_buf == NULL)
		goto failure;

	c2_bufvec_cursor_init(&zvec->z_cursor, &zvec->z_bvec);

	C2_POST(zerovec_invariant(zvec));
	return 0;
failure:
	c2_0vec_free(zvec);
	return -ENOMEM;
}
C2_EXPORTED(c2_0vec_init);

int c2_0vec_bvec_init(struct c2_0vec *zvec,
		      const struct c2_bufvec *bufvec,
		      const c2_bindex_t *indices)
{
	bool		  move = false;
	uint32_t	  i;
	c2_bcount_t	  step;
	struct c2_bufvec *bvec;

	C2_PRE(zvec != NULL);
	C2_PRE(bufvec != NULL);
	C2_PRE(indices != NULL);

	bvec = &zvec->z_bvec;
	if (c2_bufvec_cursor_move(&zvec->z_cursor, 0) ||
	    bufvec->ov_vec.v_nr > bvec->ov_vec.v_nr ||
	    c2_vec_count(&bufvec->ov_vec) > c2_vec_count(&bvec->ov_vec))
		return -EMSGSIZE;

	for (i = 0; i < bufvec->ov_vec.v_nr; ++i)
		if (bufvec->ov_vec.v_count[i] > bvec->ov_vec.v_count[i])
			return -EMSGSIZE;

	for (i = 0; i < bufvec->ov_vec.v_nr && !move; ++i) {
		zvec->z_indices[i] = indices[i];
		bvec->ov_vec.v_count[i] = bufvec->ov_vec.v_count[i];
		bvec->ov_buf[i] = bufvec->ov_buf[i];
		step = c2_bufvec_cursor_step(&zvec->z_cursor);
		move = c2_bufvec_cursor_move(&zvec->z_cursor, step);
	}

	C2_POST(zerovec_invariant(zvec));
	return i == bufvec->ov_vec.v_nr ? 0 : -EMSGSIZE;
}
C2_EXPORTED(c2_0vec_bvec_init);

int c2_0vec_bufs_init(struct c2_0vec *zvec,
		      void **bufs,
		      const c2_bindex_t *indices,
		      const c2_bcount_t *counts,
		      const uint32_t segs_nr)
{
	int		  i;
	bool		  move = false;
	c2_bcount_t	  step;
	c2_bcount_t	  total_bufsize;
	struct c2_bufvec *bvec;

	C2_PRE(zvec != NULL);
	C2_PRE(bufs != NULL);
	C2_PRE(indices != NULL);
	C2_PRE(counts != NULL);
	C2_PRE(segs_nr != 0);

	bvec = &zvec->z_bvec;
	if (c2_bufvec_cursor_move(&zvec->z_cursor, 0) ||
	    segs_nr > bvec->ov_vec.v_nr)
		return -EMSGSIZE;

	for (total_bufsize = 0, i = 0; i < segs_nr; ++i) {
		if (counts[i] > bvec->ov_vec.v_count[i])
			return -EMSGSIZE;
		total_bufsize += counts[i];
	}

	if (total_bufsize > c2_vec_count(&bvec->ov_vec))
		return -EMSGSIZE;

	for (i = 0; i < segs_nr && !move; ++i) {
		zvec->z_indices[i] = indices[i];
		bvec->ov_vec.v_count[i] = counts[i];
		bvec->ov_buf[i] = bufs[i];
		step = c2_bufvec_cursor_step(&zvec->z_cursor);
		move = c2_bufvec_cursor_move(&zvec->z_cursor, step);
	}

	C2_POST(zerovec_invariant(zvec));
	return i == segs_nr ? 0 : -EMSGSIZE;
}
C2_EXPORTED(c2_0vec_bufs_init);

int c2_0vec_cbuf_add(struct c2_0vec *zvec,
		     const struct c2_buf *buf,
		     const c2_bindex_t *index)
{
	uint32_t	  curr_seg;
	c2_bcount_t	  step;
	struct c2_bufvec *bvec;

	C2_PRE(zvec != NULL);
	C2_PRE(buf != NULL);
	C2_PRE(index != NULL);

	bvec = &zvec->z_bvec;
	curr_seg = zvec->z_cursor.bc_vc.vc_seg;

	if (c2_bufvec_cursor_move(&zvec->z_cursor, 0) ||
	    buf->b_nob > bvec->ov_vec.v_count[curr_seg])
		return -EMSGSIZE;

	bvec->ov_buf[curr_seg] = buf->b_addr;
	bvec->ov_vec.v_count[curr_seg] = buf->b_nob;
	zvec->z_indices[curr_seg] = *index;
	step = c2_bufvec_cursor_step(&zvec->z_cursor);
	c2_bufvec_cursor_move(&zvec->z_cursor, step);

	C2_POST(zerovec_invariant(zvec));
	return 0;
}
C2_EXPORTED(c2_0vec_cbuf_add);

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
