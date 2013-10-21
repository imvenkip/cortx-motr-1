/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
#include "lib/vec.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/misc.h"      /* M0_SET0, memcpy */
#include "lib/errno.h"
#include "lib/finject.h"

/**
   @addtogroup vec Vectors
   @{
*/

M0_BASSERT(M0_SEG_SIZE == M0_0VEC_ALIGN);

static m0_bcount_t vec_count(const struct m0_vec *vec, uint32_t i)
{
	m0_bcount_t count;

	for (count = 0; i < vec->v_nr; ++i) {
		/* overflow check */
		M0_ASSERT(count + vec->v_count[i] >= count);
		count += vec->v_count[i];
	}
	return count;
}

M0_INTERNAL m0_bcount_t m0_vec_count(const struct m0_vec *vec)
{
	return vec_count(vec, 0);
}

static bool m0_vec_cursor_invariant(const struct m0_vec_cursor *cur)
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
static void m0_vec_cursor_normalize(struct m0_vec_cursor *cur)
{
	while (cur->vc_seg < cur->vc_vec->v_nr &&
	       cur->vc_vec->v_count[cur->vc_seg] == 0) {
		++cur->vc_seg;
		cur->vc_offset = 0;
	}
}

M0_INTERNAL void m0_vec_cursor_init(struct m0_vec_cursor *cur,
				    struct m0_vec *vec)
{
	cur->vc_vec    = vec;
	cur->vc_seg    = 0;
	cur->vc_offset = 0;
	m0_vec_cursor_normalize(cur);
	M0_ASSERT(m0_vec_cursor_invariant(cur));
}

M0_INTERNAL bool m0_vec_cursor_move(struct m0_vec_cursor *cur,
				    m0_bcount_t count)
{
	M0_ASSERT(m0_vec_cursor_invariant(cur));
	while (count > 0 && cur->vc_seg < cur->vc_vec->v_nr) {
		m0_bcount_t step;

		step = m0_vec_cursor_step(cur);
		if (count >= step) {
			cur->vc_seg++;
			cur->vc_offset = 0;
			count -= step;
		} else {
			cur->vc_offset += count;
			count = 0;
		}
		m0_vec_cursor_normalize(cur);
	}
	M0_ASSERT(m0_vec_cursor_invariant(cur));
	return cur->vc_seg == cur->vc_vec->v_nr;
}

M0_INTERNAL m0_bcount_t m0_vec_cursor_step(const struct m0_vec_cursor *cur)
{
	M0_PRE(cur->vc_seg < cur->vc_vec->v_nr);
	M0_ASSERT(m0_vec_cursor_invariant(cur));
	return cur->vc_vec->v_count[cur->vc_seg] - cur->vc_offset;
}

M0_INTERNAL m0_bcount_t m0_vec_cursor_end(const struct m0_vec_cursor *cur)
{
	return m0_vec_cursor_step(cur) +
	       vec_count(cur->vc_vec, cur->vc_seg + 1);
}


static int m0__bufvec_alloc(struct m0_bufvec *bufvec,
	                    uint32_t          num_segs,
	                    m0_bcount_t       seg_size,
	                    unsigned	      shift)
{
	uint32_t i;

	M0_PRE(num_segs > 0 && seg_size > 0);
	bufvec->ov_buf = NULL;
	bufvec->ov_vec.v_nr = num_segs;
	M0_ALLOC_ARR(bufvec->ov_vec.v_count, num_segs);
	if (bufvec->ov_vec.v_count == NULL)
		goto fail;
	M0_ALLOC_ARR(bufvec->ov_buf, num_segs);
	if (bufvec->ov_buf == NULL)
		goto fail;

	for (i = 0; i < bufvec->ov_vec.v_nr; ++i) {
		if (shift != 0)
			bufvec->ov_buf[i] = m0_alloc_aligned(seg_size, shift);
		else
			bufvec->ov_buf[i] = m0_alloc(seg_size);

		if (bufvec->ov_buf[i] == NULL)
			goto fail;
		bufvec->ov_vec.v_count[i] = seg_size;
	}
	return 0;

fail:
	m0_bufvec_free(bufvec);
	return -ENOMEM;
}

M0_INTERNAL int m0_bufvec_alloc(struct m0_bufvec *bufvec,
				uint32_t num_segs, m0_bcount_t seg_size)
{
	return m0__bufvec_alloc(bufvec, num_segs, seg_size, 0);
}
M0_EXPORTED(m0_bufvec_alloc);

M0_INTERNAL int m0_bufvec_extend(struct m0_bufvec *bufvec,
				 uint32_t num_segs)
{
	int          i = 0;
	void       **new_buf_arr = NULL;
	uint32_t     new_num_segs;
	m0_bcount_t  seg_size;
	m0_bcount_t *new_seg_count_arr;

	M0_PRE(num_segs > 0);
	M0_PRE(bufvec != NULL);
	M0_PRE(bufvec->ov_buf != NULL);
	M0_PRE(bufvec->ov_vec.v_nr > 0);

	/* Based on assumption that all segment sizes are equal */
	seg_size = bufvec->ov_vec.v_count[0];
	new_num_segs = bufvec->ov_vec.v_nr + num_segs;
	M0_ALLOC_ARR(new_seg_count_arr, new_num_segs);
	if (new_seg_count_arr == NULL)
		goto fail;
	memcpy(new_seg_count_arr,
		bufvec->ov_vec.v_count,
		bufvec->ov_vec.v_nr * sizeof(new_seg_count_arr[0]));
	M0_ALLOC_ARR(new_buf_arr, new_num_segs);
	if (new_buf_arr == NULL)
		goto fail;
	memcpy(new_buf_arr,
		bufvec->ov_buf,
		bufvec->ov_vec.v_nr * sizeof(new_buf_arr[0]));
	for (i = bufvec->ov_vec.v_nr; i < new_num_segs; ++i) {
		new_buf_arr[i] = m0_alloc(seg_size);
		if (new_buf_arr[i] == NULL)
			goto fail;
		new_seg_count_arr[i] = seg_size;
	}
	m0_free(bufvec->ov_vec.v_count);
	m0_free(bufvec->ov_buf);
	bufvec->ov_vec.v_count = new_seg_count_arr;
	bufvec->ov_buf = new_buf_arr;
	bufvec->ov_vec.v_nr = new_num_segs;

	return 0;
fail:
	m0_free(new_seg_count_arr);
	while (i > 0) {
		--i;
		m0_free(new_buf_arr[i]);
	}
	m0_free(new_buf_arr);

	return -ENOMEM;
}
M0_EXPORTED(m0_bufvec_extend);

M0_INTERNAL int m0_bufvec_merge(struct m0_bufvec *dst_bufvec,
                                struct m0_bufvec *src_bufvec)
{
	uint32_t      i;
	uint32_t      new_v_nr;
	m0_bcount_t  *new_v_count;
	void        **new_buf;

	M0_PRE(dst_bufvec != NULL);
	M0_PRE(src_bufvec != NULL);
	M0_PRE(dst_bufvec->ov_buf != NULL);
	M0_PRE(src_bufvec->ov_buf != NULL);
	M0_PRE(dst_bufvec->ov_vec.v_nr > 0);
	M0_PRE(src_bufvec->ov_vec.v_nr > 0);

	new_v_nr = dst_bufvec->ov_vec.v_nr + src_bufvec->ov_vec.v_nr;
	M0_ALLOC_ARR(new_v_count, new_v_nr);
	if (new_v_count == NULL)
		return -ENOMEM;

	M0_ALLOC_ARR(new_buf, new_v_nr);
	if (new_buf == NULL) {
		m0_free(new_v_count);
		return -ENOMEM;
	}

	for (i = 0; i < dst_bufvec->ov_vec.v_nr; ++i) {
		new_v_count[i] = dst_bufvec->ov_vec.v_count[i];
		new_buf[i] = dst_bufvec->ov_buf[i];
	}

	for (i = 0; i < src_bufvec->ov_vec.v_nr; ++i) {
		new_v_count[dst_bufvec->ov_vec.v_nr + i] =
			src_bufvec->ov_vec.v_count[i];
		new_buf[dst_bufvec->ov_vec.v_nr + i] =
			src_bufvec->ov_buf[i];
	}

	m0_free(dst_bufvec->ov_vec.v_count);
	m0_free(dst_bufvec->ov_buf);
	dst_bufvec->ov_vec.v_nr = new_v_nr;
	dst_bufvec->ov_vec.v_count = new_v_count;
	dst_bufvec->ov_buf = new_buf;

	return 0;
}
M0_EXPORTED(m0_bufvec_merge);

M0_INTERNAL int m0_bufvec_alloc_aligned(struct m0_bufvec *bufvec,
					uint32_t num_segs,
					m0_bcount_t seg_size, unsigned shift)
{
	if (M0_FI_ENABLED("oom"))
		return -ENOMEM;

	return m0__bufvec_alloc(bufvec, num_segs, seg_size, shift);
}
M0_EXPORTED(m0_bufvec_alloc_aligned);

M0_INTERNAL void m0_bufvec_free(struct m0_bufvec *bufvec)
{
	if (bufvec != NULL) {
		if (bufvec->ov_buf != NULL) {
			uint32_t i;

			for (i = 0; i < bufvec->ov_vec.v_nr; ++i)
				m0_free(bufvec->ov_buf[i]);
			m0_free(bufvec->ov_buf);
		}
		m0_free(bufvec->ov_vec.v_count);
		M0_SET0(bufvec);
	}
}
M0_EXPORTED(m0_bufvec_free);

M0_INTERNAL void m0_bufvec_free_aligned(struct m0_bufvec *bufvec,
					unsigned shift)
{
	if (shift == 0)
		m0_bufvec_free(bufvec);
	else if (bufvec != NULL) {
		if (bufvec->ov_buf != NULL) {
			uint32_t i;
			for (i = 0; i < bufvec->ov_vec.v_nr; ++i)
				m0_free_aligned(bufvec->ov_buf[i],
					bufvec->ov_vec.v_count[i], shift);
			m0_free(bufvec->ov_buf);
		}
		m0_free(bufvec->ov_vec.v_count);
		M0_SET0(bufvec);
	}
}
M0_EXPORTED(m0_bufvec_free_aligned);

static uint32_t vec_pack(struct m0_vec *vec, m0_bindex_t *idx)
{
	uint32_t i = 0;
	uint32_t j;

	if (vec->v_nr == 0)
		return 0;

	for (j = i + 1; j < vec->v_nr; ++j) {
		if (idx[i] + vec->v_count[i] == idx[j]) {
			vec->v_count[i] += vec->v_count[j];
		} else {
			++i;
			if (i != j) {
				idx[i] = idx[j];
				vec->v_count[i] = vec->v_count[j];
			}
		}
	}
	vec->v_nr = i + 1;

	return j - vec->v_nr;
}

M0_INTERNAL uint32_t m0_bufvec_pack(struct m0_bufvec *bv)
{
	return vec_pack(&bv->ov_vec, (m0_bindex_t*)bv->ov_buf);
}

M0_INTERNAL uint32_t m0_indexvec_pack(struct m0_indexvec *iv)
{
	return vec_pack(&iv->iv_vec, iv->iv_index);
}

M0_INTERNAL int m0_indexvec_alloc(struct m0_indexvec *ivec,
				  uint32_t len,
				  struct m0_addb_ctx *ctx,
				  const unsigned loc)
{
	M0_PRE(ivec != NULL);
	M0_PRE(len   > 0);

	M0_ALLOC_ARR_ADDB(ivec->iv_index, len, &m0_addb_gmc, loc, ctx);
	if (ivec->iv_index == NULL)
		return -ENOMEM;

	M0_ALLOC_ARR_ADDB(ivec->iv_vec.v_count, len, &m0_addb_gmc, loc, ctx);
	if (ivec->iv_vec.v_count == NULL) {
		m0_free(ivec->iv_index);
		return -ENOMEM;
	}

	ivec->iv_vec.v_nr = len;
	return 0;
}

M0_INTERNAL void m0_indexvec_free(struct m0_indexvec *ivec)
{
	M0_PRE(ivec != NULL);
	M0_PRE(ivec->iv_vec.v_nr > 0);

	if (ivec->iv_index != NULL) {
		m0_free(ivec->iv_index);
		ivec->iv_index = NULL;
	}

	if (ivec->iv_vec.v_count != NULL) {
		m0_free(ivec->iv_vec.v_count);
		ivec->iv_vec.v_count = NULL;
	}
	ivec->iv_vec.v_nr = 0;
}

M0_INTERNAL void m0_bufvec_cursor_init(struct m0_bufvec_cursor *cur,
				       struct m0_bufvec *bvec)
{
	M0_PRE(cur != NULL);
	M0_PRE(bvec != NULL &&
	       bvec->ov_vec.v_nr != 0 && bvec->ov_vec.v_count != NULL &&
	       bvec->ov_buf != NULL);
	m0_vec_cursor_init(&cur->bc_vc, &bvec->ov_vec);
}
M0_EXPORTED(m0_bufvec_cursor_init);

M0_INTERNAL bool m0_bufvec_cursor_move(struct m0_bufvec_cursor *cur,
				       m0_bcount_t count)
{
	return m0_vec_cursor_move(&cur->bc_vc, count);
}
M0_EXPORTED(m0_bufvec_cursor_move);

M0_INTERNAL m0_bcount_t m0_bufvec_cursor_step(const struct m0_bufvec_cursor
					      *cur)
{
	return m0_vec_cursor_step(&cur->bc_vc);
}
M0_EXPORTED(m0_bufvec_cursor_step);

M0_INTERNAL void *bufvec_cursor_addr(struct m0_bufvec_cursor *cur)
{
	struct m0_vec_cursor *vc = &cur->bc_vc;
	struct m0_bufvec *bv = container_of(vc->vc_vec,struct m0_bufvec,ov_vec);

	M0_PRE(!m0_bufvec_cursor_move(cur, 0));
	return bv->ov_buf[vc->vc_seg] + vc->vc_offset;
}

M0_INTERNAL void *m0_bufvec_cursor_addr(struct m0_bufvec_cursor *cur)
{
	M0_PRE(!m0_bufvec_cursor_move(cur, 0));
	return bufvec_cursor_addr(cur);
}
M0_EXPORTED(m0_bufvec_cursor_addr);

M0_INTERNAL bool m0_bufvec_cursor_align(struct m0_bufvec_cursor *cur,
					uint64_t alignment)
{
	uint64_t addr;
	uint64_t count;

	if (m0_bufvec_cursor_move(cur, 0))
		return true;

	addr = (uint64_t)m0_bufvec_cursor_addr(cur);
	count = m0_align(addr, alignment) - addr;

	return m0_bufvec_cursor_move(cur, count);
}
M0_EXPORTED(m0_bufvec_cursor_align);

M0_INTERNAL m0_bcount_t m0_bufvec_cursor_copy(struct m0_bufvec_cursor *dcur,
					      struct m0_bufvec_cursor *scur,
					      m0_bcount_t num_bytes)
{
	m0_bcount_t frag_size    = 0;
	m0_bcount_t bytes_copied = 0;
	/* bitwise OR used below to ensure both cursors get moved
	   without short-circuit logic, also why cursor move is before
	   simpler num_bytes check */
	while (!(m0_bufvec_cursor_move(dcur, frag_size) |
		 m0_bufvec_cursor_move(scur, frag_size)) &&
	       num_bytes > 0) {
		frag_size = min3(m0_bufvec_cursor_step(dcur),
				 m0_bufvec_cursor_step(scur),
				 num_bytes);
		memmove(bufvec_cursor_addr(dcur),
			bufvec_cursor_addr(scur),
			frag_size);
		num_bytes -= frag_size;
		bytes_copied += frag_size;
	}
	return bytes_copied;
}
M0_EXPORTED(m0_bufvec_cursor_copy);

M0_INTERNAL m0_bcount_t m0_bufvec_cursor_copyto(struct m0_bufvec_cursor *dcur,
						void *sdata,
						m0_bcount_t num_bytes)
{
	struct m0_bufvec_cursor scur;
	struct m0_bufvec        sbuf = M0_BUFVEC_INIT_BUF(&sdata, &num_bytes);

	M0_PRE(dcur != NULL);
	M0_PRE(sdata != NULL);

	m0_bufvec_cursor_init(&scur, &sbuf);

	return m0_bufvec_cursor_copy(dcur, &scur, num_bytes);
}

M0_INTERNAL m0_bcount_t m0_bufvec_cursor_copyfrom(struct m0_bufvec_cursor *scur,
						  void *ddata,
						  m0_bcount_t num_bytes)
{
	struct m0_bufvec_cursor dcur;
	struct m0_bufvec        dbuf = M0_BUFVEC_INIT_BUF(&ddata, &num_bytes);

	M0_PRE(scur != NULL);
	M0_PRE(ddata != NULL);

	m0_bufvec_cursor_init(&dcur, &dbuf);

	return m0_bufvec_cursor_copy(&dcur, scur, num_bytes);
}

M0_INTERNAL void m0_ivec_cursor_init(struct m0_ivec_cursor *cur,
				     struct m0_indexvec *ivec)
{
        M0_PRE(cur  != NULL);
        M0_PRE(ivec != NULL);
        M0_PRE(ivec->iv_vec.v_nr > 0);
        M0_PRE(ivec->iv_vec.v_count != NULL && ivec->iv_index != NULL);

        m0_vec_cursor_init(&cur->ic_cur, &ivec->iv_vec);
}

M0_INTERNAL bool m0_ivec_cursor_move(struct m0_ivec_cursor *cur,
				     m0_bcount_t count)
{
        M0_PRE(cur != NULL);

        return m0_vec_cursor_move(&cur->ic_cur, count);
}

M0_INTERNAL m0_bcount_t m0_ivec_cursor_step(const struct m0_ivec_cursor *cur)
{
        M0_PRE(cur != NULL);

        return m0_vec_cursor_step(&cur->ic_cur);
}

M0_INTERNAL m0_bindex_t m0_ivec_cursor_index(struct m0_ivec_cursor *cur)
{
        struct m0_indexvec *ivec;

        M0_PRE(cur != NULL);
	M0_PRE(!m0_vec_cursor_move(&cur->ic_cur, 0));

        ivec = container_of(cur->ic_cur.vc_vec, struct m0_indexvec, iv_vec);
        return ivec->iv_index[cur->ic_cur.vc_seg] + cur->ic_cur.vc_offset;
}

M0_INTERNAL bool m0_ivec_cursor_move_to(struct m0_ivec_cursor *cur,
					m0_bindex_t dest)
{
	m0_bindex_t min;
	bool        ret = false;

	M0_PRE(cur  != NULL);
	M0_PRE(dest >= m0_ivec_cursor_index(cur));

	while (m0_ivec_cursor_index(cur) != dest) {
		min = min64u(dest, m0_ivec_cursor_index(cur) +
			     m0_ivec_cursor_step(cur));
		ret = m0_ivec_cursor_move(cur, min - m0_ivec_cursor_index(cur));
		if (ret)
			break;
	}
	return ret;
}

M0_INTERNAL void m0_0vec_fini(struct m0_0vec *zvec)
{
	if (zvec != NULL) {
		m0_free(zvec->z_bvec.ov_vec.v_count);
		m0_free(zvec->z_bvec.ov_buf);
		m0_free(zvec->z_index);
	}
}

static bool addr_is_4k_aligned(void *addr)
{
	return ((uint64_t)addr & M0_0VEC_MASK) == 0;
}

static bool m0_0vec_invariant(const struct m0_0vec *zvec)
{
	const struct m0_bufvec *bvec = &zvec->z_bvec;

	return zvec != NULL && zvec->z_index != NULL &&
		bvec->ov_buf != NULL &&
		bvec->ov_vec.v_count != NULL &&
		bvec->ov_vec.v_nr != 0 &&
		/*
		 * All segments are aligned on 4k boundary and the sizes of all
		 * segments except the last one are positive multiples of 4k.
		 */
		m0_forall(i, bvec->ov_vec.v_nr,
			  addr_is_4k_aligned(bvec->ov_buf[i]) &&
			  ergo(i < bvec->ov_vec.v_nr - 1,
			       !(bvec->ov_vec.v_count[i] & M0_0VEC_MASK)));
}

M0_INTERNAL int m0_0vec_init(struct m0_0vec *zvec, uint32_t segs_nr)
{
	M0_PRE(zvec != NULL);
	M0_PRE(segs_nr != 0);

	M0_SET0(zvec);
	M0_ALLOC_ARR(zvec->z_index, segs_nr);
	if (zvec->z_index == NULL)
		return -ENOMEM;

	M0_ALLOC_ARR(zvec->z_bvec.ov_vec.v_count, segs_nr);
	if (zvec->z_bvec.ov_vec.v_count == NULL)
		goto failure;

	zvec->z_bvec.ov_vec.v_nr = segs_nr;

	M0_ALLOC_ARR(zvec->z_bvec.ov_buf, segs_nr);
	if (zvec->z_bvec.ov_buf == NULL)
		goto failure;

	return 0;
failure:
	m0_0vec_fini(zvec);
	return -ENOMEM;
}

M0_INTERNAL void m0_0vec_bvec_init(struct m0_0vec *zvec,
				   const struct m0_bufvec *src,
				   const m0_bindex_t * index)
{
	uint32_t	  i;
	struct m0_bufvec *dst;

	M0_PRE(zvec != NULL);
	M0_PRE(src != NULL);
	M0_PRE(index != NULL);
	M0_PRE(src->ov_vec.v_nr <= zvec->z_bvec.ov_vec.v_nr);

	dst = &zvec->z_bvec;
	for (i = 0; i < src->ov_vec.v_nr; ++i) {
		zvec->z_index[i] = index[i];
		dst->ov_vec.v_count[i] = src->ov_vec.v_count[i];
		M0_ASSERT(dst->ov_buf[i] == NULL);
		dst->ov_buf[i] = src->ov_buf[i];
	}

	M0_POST(m0_0vec_invariant(zvec));
}

M0_INTERNAL void m0_0vec_bufs_init(struct m0_0vec *zvec,
				   void **bufs,
				   const m0_bindex_t * index,
				   const m0_bcount_t * counts, uint32_t segs_nr)
{
	uint32_t	  i;
	struct m0_bufvec *bvec;

	M0_PRE(zvec != NULL);
	M0_PRE(bufs != NULL);
	M0_PRE(index != NULL);
	M0_PRE(counts != NULL);
	M0_PRE(segs_nr != 0);
	M0_PRE(segs_nr <= zvec->z_bvec.ov_vec.v_nr);

	bvec = &zvec->z_bvec;

	for (i = 0; i < segs_nr; ++i) {
		zvec->z_index[i] = index[i];
		bvec->ov_vec.v_count[i] = counts[i];
		M0_ASSERT(bvec->ov_buf[i] == NULL);
		bvec->ov_buf[i] = bufs[i];
	}

	M0_POST(m0_0vec_invariant(zvec));
}

M0_INTERNAL int m0_0vec_cbuf_add(struct m0_0vec *zvec,
				 const struct m0_buf *buf,
				 const m0_bindex_t * index)
{
	uint32_t	  curr_seg;
	struct m0_bufvec *bvec;

	M0_PRE(zvec != NULL);
	M0_PRE(buf != NULL);
	M0_PRE(index != NULL);

	bvec = &zvec->z_bvec;
	for (curr_seg = 0; curr_seg < bvec->ov_vec.v_nr &&
	     bvec->ov_buf[curr_seg] != NULL; ++curr_seg);

	if (curr_seg == bvec->ov_vec.v_nr)
		return -EMSGSIZE;

	M0_ASSERT(bvec->ov_buf[curr_seg] == NULL);
	bvec->ov_buf[curr_seg] = buf->b_addr;
	bvec->ov_vec.v_count[curr_seg] = buf->b_nob;
	zvec->z_index[curr_seg] = *index;

	M0_POST(m0_0vec_invariant(zvec));
	return 0;
}

/**
 * Initializes a m0_bufvec containing a single element of specified size
 */
static void data_to_bufvec(struct m0_bufvec *src_buf, void **data,
			   size_t *len)
{
	M0_PRE(src_buf != NULL);
	M0_PRE(len != 0);
	M0_PRE(data != NULL);
	M0_CASSERT(sizeof len == sizeof src_buf->ov_vec.v_count);

	src_buf->ov_vec.v_nr = 1;
	src_buf->ov_vec.v_count = (m0_bcount_t *)len;
	src_buf->ov_buf = data;
}

M0_INTERNAL int m0_data_to_bufvec_copy(struct m0_bufvec_cursor *cur, void *data,
				       size_t len)
{
	m0_bcount_t		count;
	struct m0_bufvec_cursor src_cur;
	struct m0_bufvec	src_buf;

	M0_PRE(cur  != NULL);
	M0_PRE(data != NULL);

	data_to_bufvec(&src_buf, &data, &len);
	m0_bufvec_cursor_init(&src_cur, &src_buf);
	count = m0_bufvec_cursor_copy(cur, &src_cur, len);
	if (count != len)
		return -EFAULT;
	return 0;
}

M0_INTERNAL int m0_bufvec_to_data_copy(struct m0_bufvec_cursor *cur, void *data,
				       size_t len)
{
	m0_bcount_t		count;
	struct m0_bufvec_cursor dcur;
	struct m0_bufvec	dest_buf;

	M0_PRE(cur  != NULL);
	M0_PRE(data != NULL);
	M0_PRE(len  != 0);

	data_to_bufvec(&dest_buf, &data, &len);
	m0_bufvec_cursor_init(&dcur, &dest_buf);
	count = m0_bufvec_cursor_copy(&dcur, cur, len);
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
