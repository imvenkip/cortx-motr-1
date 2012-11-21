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
#include "lib/finject.h"

/**
   @addtogroup vec Vectors
   @{
*/

C2_BASSERT(C2_SEG_SIZE == C2_0VEC_ALIGN);

C2_INTERNAL c2_bcount_t c2_vec_count(const struct c2_vec *vec)
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

C2_INTERNAL void c2_vec_cursor_init(struct c2_vec_cursor *cur,
				    struct c2_vec *vec)
{
	cur->vc_vec    = vec;
	cur->vc_seg    = 0;
	cur->vc_offset = 0;
	c2_vec_cursor_normalize(cur);
	C2_ASSERT(c2_vec_cursor_invariant(cur));
}

C2_INTERNAL bool c2_vec_cursor_move(struct c2_vec_cursor *cur,
				    c2_bcount_t count)
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

C2_INTERNAL c2_bcount_t c2_vec_cursor_step(const struct c2_vec_cursor *cur)
{
	C2_PRE(cur->vc_seg < cur->vc_vec->v_nr);
	C2_ASSERT(c2_vec_cursor_invariant(cur));
	return cur->vc_vec->v_count[cur->vc_seg] - cur->vc_offset;
}


static int c2__bufvec_alloc(struct c2_bufvec *bufvec,
	                    uint32_t          num_segs,
	                    c2_bcount_t       seg_size,
	                    unsigned	      shift)
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
		if (shift != 0)
			bufvec->ov_buf[i] = c2_alloc_aligned(seg_size, shift);
		else
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

C2_INTERNAL int c2_bufvec_alloc(struct c2_bufvec *bufvec,
				uint32_t num_segs, c2_bcount_t seg_size)
{
	return c2__bufvec_alloc(bufvec, num_segs, seg_size, 0);
}
C2_EXPORTED(c2_bufvec_alloc);

C2_INTERNAL int c2_bufvec_alloc_aligned(struct c2_bufvec *bufvec,
					uint32_t num_segs,
					c2_bcount_t seg_size, unsigned shift)
{
	if (C2_FI_ENABLED("oom"))
		return -ENOMEM;

	return c2__bufvec_alloc(bufvec, num_segs, seg_size, shift);
}
C2_EXPORTED(c2_bufvec_alloc_aligned);

C2_INTERNAL void c2_bufvec_free(struct c2_bufvec *bufvec)
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

C2_INTERNAL void c2_bufvec_free_aligned(struct c2_bufvec *bufvec,
					unsigned shift)
{
	if (shift == 0)
		c2_bufvec_free(bufvec);
	else if (bufvec != NULL) {
		if (bufvec->ov_buf != NULL) {
			uint32_t i;
			for (i = 0; i < bufvec->ov_vec.v_nr; ++i)
				c2_free_aligned(bufvec->ov_buf[i],
					bufvec->ov_vec.v_count[i], shift);
			c2_free(bufvec->ov_buf);
		}
		c2_free(bufvec->ov_vec.v_count);
		C2_SET0(bufvec);
	}
}
C2_EXPORTED(c2_bufvec_free_aligned);

C2_INTERNAL int c2_indexvec_alloc(struct c2_indexvec *ivec,
				  uint32_t len,
				  struct c2_addb_ctx *ctx,
				  const struct c2_addb_loc *loc)
{
	C2_PRE(ivec != NULL);
	C2_PRE(len   > 0);

	C2_ALLOC_ARR_ADDB(ivec->iv_index, len, ctx, loc);
	if (ivec->iv_index == NULL)
		return -ENOMEM;

	C2_ALLOC_ARR_ADDB(ivec->iv_vec.v_count, len, ctx, loc);
	if (ivec->iv_vec.v_count == NULL) {
		c2_free(ivec->iv_index);
		return -ENOMEM;
	}

	ivec->iv_vec.v_nr = len;
	return 0;
}

C2_INTERNAL void c2_indexvec_free(struct c2_indexvec *ivec)
{
	C2_PRE(ivec != NULL);
	C2_PRE(ivec->iv_vec.v_nr > 0);

	if (ivec->iv_index != NULL) {
		c2_free(ivec->iv_index);
		ivec->iv_index = NULL;
	}

	if (ivec->iv_vec.v_count != NULL) {
		c2_free(ivec->iv_vec.v_count);
		ivec->iv_vec.v_count = NULL;
	}
	ivec->iv_vec.v_nr = 0;
}

C2_INTERNAL void c2_bufvec_cursor_init(struct c2_bufvec_cursor *cur,
				       struct c2_bufvec *bvec)
{
	C2_PRE(cur != NULL);
	C2_PRE(bvec != NULL &&
	       bvec->ov_vec.v_nr != 0 && bvec->ov_vec.v_count != NULL &&
	       bvec->ov_buf != NULL);
	c2_vec_cursor_init(&cur->bc_vc, &bvec->ov_vec);
}
C2_EXPORTED(c2_bufvec_cursor_init);

C2_INTERNAL bool c2_bufvec_cursor_move(struct c2_bufvec_cursor *cur,
				       c2_bcount_t count)
{
	return c2_vec_cursor_move(&cur->bc_vc, count);
}
C2_EXPORTED(c2_bufvec_cursor_move);

C2_INTERNAL c2_bcount_t c2_bufvec_cursor_step(const struct c2_bufvec_cursor
					      *cur)
{
	return c2_vec_cursor_step(&cur->bc_vc);
}
C2_EXPORTED(c2_bufvec_cursor_step);

C2_INTERNAL void *bufvec_cursor_addr(struct c2_bufvec_cursor *cur)
{
	struct c2_vec_cursor *vc = &cur->bc_vc;
	struct c2_bufvec *bv = container_of(vc->vc_vec,struct c2_bufvec,ov_vec);

	C2_PRE(!c2_bufvec_cursor_move(cur, 0));
	return bv->ov_buf[vc->vc_seg] + vc->vc_offset;
}

C2_INTERNAL void *c2_bufvec_cursor_addr(struct c2_bufvec_cursor *cur)
{
	C2_PRE(!c2_bufvec_cursor_move(cur, 0));
	return bufvec_cursor_addr(cur);
}
C2_EXPORTED(c2_bufvec_cursor_addr);

C2_INTERNAL bool c2_bufvec_cursor_align(struct c2_bufvec_cursor *cur,
					uint64_t alignment)
{
	uint64_t addr;
	uint64_t count;

	if (c2_bufvec_cursor_move(cur, 0))
		return true;

	addr = (uint64_t)c2_bufvec_cursor_addr(cur);
	count = c2_align(addr, alignment) - addr;

	return c2_bufvec_cursor_move(cur, count);
}
C2_EXPORTED(c2_bufvec_cursor_align);

C2_INTERNAL c2_bcount_t c2_bufvec_cursor_copy(struct c2_bufvec_cursor *dcur,
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

C2_INTERNAL c2_bcount_t c2_bufvec_cursor_copyto(struct c2_bufvec_cursor *dcur,
						void *sdata,
						c2_bcount_t num_bytes)
{
	struct c2_bufvec_cursor scur;
	struct c2_bufvec        sbuf = C2_BUFVEC_INIT_BUF(&sdata, &num_bytes);

	C2_PRE(dcur != NULL);
	C2_PRE(sdata != NULL);

	c2_bufvec_cursor_init(&scur, &sbuf);

	return c2_bufvec_cursor_copy(dcur, &scur, num_bytes);
}

C2_INTERNAL c2_bcount_t c2_bufvec_cursor_copyfrom(struct c2_bufvec_cursor *scur,
						  void *ddata,
						  c2_bcount_t num_bytes)
{
	struct c2_bufvec_cursor dcur;
	struct c2_bufvec        dbuf = C2_BUFVEC_INIT_BUF(&ddata, &num_bytes);

	C2_PRE(scur != NULL);
	C2_PRE(ddata != NULL);

	c2_bufvec_cursor_init(&dcur, &dbuf);

	return c2_bufvec_cursor_copy(&dcur, scur, num_bytes);
}

C2_INTERNAL void c2_ivec_cursor_init(struct c2_ivec_cursor *cur,
				     struct c2_indexvec *ivec)
{
        C2_PRE(cur  != NULL);
        C2_PRE(ivec != NULL);
        C2_PRE(ivec->iv_vec.v_nr > 0);
        C2_PRE(ivec->iv_vec.v_count != NULL && ivec->iv_index != NULL);

        c2_vec_cursor_init(&cur->ic_cur, &ivec->iv_vec);
}

C2_INTERNAL bool c2_ivec_cursor_move(struct c2_ivec_cursor *cur,
				     c2_bcount_t count)
{
        C2_PRE(cur != NULL);

        return c2_vec_cursor_move(&cur->ic_cur, count);
}

C2_INTERNAL c2_bcount_t c2_ivec_cursor_step(const struct c2_ivec_cursor *cur)
{
        C2_PRE(cur != NULL);

        return c2_vec_cursor_step(&cur->ic_cur);
}

C2_INTERNAL c2_bindex_t c2_ivec_cursor_index(struct c2_ivec_cursor *cur)
{
        struct c2_indexvec *ivec;

        C2_PRE(cur != NULL);
	C2_PRE(!c2_vec_cursor_move(&cur->ic_cur, 0));

        ivec = container_of(cur->ic_cur.vc_vec, struct c2_indexvec, iv_vec);
        return ivec->iv_index[cur->ic_cur.vc_seg] + cur->ic_cur.vc_offset;
}

C2_INTERNAL bool c2_ivec_cursor_move_to(struct c2_ivec_cursor *cur,
					c2_bindex_t dest)
{
	c2_bindex_t min;
	bool        ret = false;

	C2_PRE(cur  != NULL);
	C2_PRE(dest >= c2_ivec_cursor_index(cur));

	while (c2_ivec_cursor_index(cur) != dest) {
		min = min64u(dest, c2_ivec_cursor_index(cur) +
			     c2_ivec_cursor_step(cur));
		ret = c2_ivec_cursor_move(cur, min - c2_ivec_cursor_index(cur));
		if (ret)
			break;
	}
	return ret;
}

C2_INTERNAL void c2_0vec_fini(struct c2_0vec *zvec)
{
	if (zvec != NULL) {
		c2_free(zvec->z_bvec.ov_vec.v_count);
		c2_free(zvec->z_bvec.ov_buf);
		c2_free(zvec->z_index);
	}
}

static bool addr_is_4k_aligned(void *addr)
{
	return ((uint64_t)addr & C2_0VEC_MASK) == 0;
}

static bool c2_0vec_invariant(const struct c2_0vec *zvec)
{
	const struct c2_bufvec *bvec = &zvec->z_bvec;

	return zvec != NULL && zvec->z_index != NULL &&
		bvec->ov_buf != NULL &&
		bvec->ov_vec.v_count != NULL &&
		bvec->ov_vec.v_nr != 0 &&
		/*
		 * All segments are aligned on 4k boundary and the sizes of all
		 * segments except the last one are positive multiples of 4k.
		 */
		c2_forall(i, bvec->ov_vec.v_nr,
			  addr_is_4k_aligned(bvec->ov_buf[i]) &&
			  ergo(i < bvec->ov_vec.v_nr - 1,
			       !(bvec->ov_vec.v_count[i] & C2_0VEC_MASK)));
}

C2_INTERNAL int c2_0vec_init(struct c2_0vec *zvec, uint32_t segs_nr)
{
	C2_PRE(zvec != NULL);
	C2_PRE(segs_nr != 0);

	C2_SET0(zvec);
	C2_ALLOC_ARR(zvec->z_index, segs_nr);
	if (zvec->z_index == NULL)
		return -ENOMEM;

	C2_ALLOC_ARR(zvec->z_bvec.ov_vec.v_count, segs_nr);
	if (zvec->z_bvec.ov_vec.v_count == NULL)
		goto failure;

	zvec->z_bvec.ov_vec.v_nr = segs_nr;

	C2_ALLOC_ARR(zvec->z_bvec.ov_buf, segs_nr);
	if (zvec->z_bvec.ov_buf == NULL)
		goto failure;

	return 0;
failure:
	c2_0vec_fini(zvec);
	return -ENOMEM;
}

C2_INTERNAL void c2_0vec_bvec_init(struct c2_0vec *zvec,
				   const struct c2_bufvec *src,
				   const c2_bindex_t * index)
{
	uint32_t	  i;
	struct c2_bufvec *dst;

	C2_PRE(zvec != NULL);
	C2_PRE(src != NULL);
	C2_PRE(index != NULL);
	C2_PRE(src->ov_vec.v_nr <= zvec->z_bvec.ov_vec.v_nr);

	dst = &zvec->z_bvec;
	for (i = 0; i < src->ov_vec.v_nr; ++i) {
		zvec->z_index[i] = index[i];
		dst->ov_vec.v_count[i] = src->ov_vec.v_count[i];
		C2_ASSERT(dst->ov_buf[i] == NULL);
		dst->ov_buf[i] = src->ov_buf[i];
	}

	C2_POST(c2_0vec_invariant(zvec));
}

C2_INTERNAL void c2_0vec_bufs_init(struct c2_0vec *zvec,
				   void **bufs,
				   const c2_bindex_t * index,
				   const c2_bcount_t * counts, uint32_t segs_nr)
{
	uint32_t	  i;
	struct c2_bufvec *bvec;

	C2_PRE(zvec != NULL);
	C2_PRE(bufs != NULL);
	C2_PRE(index != NULL);
	C2_PRE(counts != NULL);
	C2_PRE(segs_nr != 0);
	C2_PRE(segs_nr <= zvec->z_bvec.ov_vec.v_nr);

	bvec = &zvec->z_bvec;

	for (i = 0; i < segs_nr; ++i) {
		zvec->z_index[i] = index[i];
		bvec->ov_vec.v_count[i] = counts[i];
		C2_ASSERT(bvec->ov_buf[i] == NULL);
		bvec->ov_buf[i] = bufs[i];
	}

	C2_POST(c2_0vec_invariant(zvec));
}

C2_INTERNAL int c2_0vec_cbuf_add(struct c2_0vec *zvec,
				 const struct c2_buf *buf,
				 const c2_bindex_t * index)
{
	uint32_t	  curr_seg;
	struct c2_bufvec *bvec;

	C2_PRE(zvec != NULL);
	C2_PRE(buf != NULL);
	C2_PRE(index != NULL);

	bvec = &zvec->z_bvec;
	for (curr_seg = 0; curr_seg < bvec->ov_vec.v_nr &&
	     bvec->ov_buf[curr_seg] != NULL; ++curr_seg);

	if (curr_seg == bvec->ov_vec.v_nr)
		return -EMSGSIZE;

	C2_ASSERT(bvec->ov_buf[curr_seg] == NULL);
	bvec->ov_buf[curr_seg] = buf->b_addr;
	bvec->ov_vec.v_count[curr_seg] = buf->b_nob;
	zvec->z_index[curr_seg] = *index;

	C2_POST(c2_0vec_invariant(zvec));
	return 0;
}

/**
 * Initializes a c2_bufvec containing a single element of specified size
 */
static void data_to_bufvec(struct c2_bufvec *src_buf, void **data,
			   size_t *len)
{
	C2_PRE(src_buf != NULL);
	C2_PRE(len != 0);
	C2_PRE(data != NULL);
	C2_CASSERT(sizeof len == sizeof src_buf->ov_vec.v_count);

	src_buf->ov_vec.v_nr = 1;
	src_buf->ov_vec.v_count = (c2_bcount_t *)len;
	src_buf->ov_buf = data;
}

C2_INTERNAL int c2_data_to_bufvec_copy(struct c2_bufvec_cursor *cur, void *data,
				       size_t len)
{
	c2_bcount_t		count;
	struct c2_bufvec_cursor src_cur;
	struct c2_bufvec	src_buf;

	C2_PRE(cur  != NULL);
	C2_PRE(data != NULL);

	data_to_bufvec(&src_buf, &data, &len);
	c2_bufvec_cursor_init(&src_cur, &src_buf);
	count = c2_bufvec_cursor_copy(cur, &src_cur, len);
	if (count != len)
		return -EFAULT;
	return 0;
}

C2_INTERNAL int c2_bufvec_to_data_copy(struct c2_bufvec_cursor *cur, void *data,
				       size_t len)
{
	c2_bcount_t		count;
	struct c2_bufvec_cursor dcur;
	struct c2_bufvec	dest_buf;

	C2_PRE(cur  != NULL);
	C2_PRE(data != NULL);
	C2_PRE(len  != 0);

	data_to_bufvec(&dest_buf, &data, &len);
	c2_bufvec_cursor_init(&dcur, &dest_buf);
	count = c2_bufvec_cursor_copy(&dcur, cur, len);
	if (count != len)
		return -EFAULT;
	return 0;
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
