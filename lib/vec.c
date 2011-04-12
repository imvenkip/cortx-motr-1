/* -*- C -*- */

#include "lib/cdefs.h"     /* NULL */
#include "lib/vec.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/misc.h"
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
