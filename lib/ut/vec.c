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

#include "lib/ut.h"
#include "lib/cdefs.h"     /* ARRAY_SIZE */
#include "lib/vec.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/assert.h"

static void test_bufvec_cursor(void);

enum {
	NR = 255,
	IT = 6,
};

static c2_bcount_t segs[NR * IT];

static struct c2_vec t = {
	.v_nr    = NR * IT,
	.v_count = segs
};

void test_vec(void)
{
	int         i;
	int         it;
	c2_bcount_t count;
	c2_bcount_t sum0;
	c2_bcount_t sum1;
	c2_bcount_t step;
	bool        eov;

	struct c2_vec_cursor c;
	struct c2_bufvec     bv;

	for (count = 0, it = 1, sum0 = i = 0; i < ARRAY_SIZE(segs); ++i) {
		segs[i] = count * it;
		sum0 += segs[i];
		if (++count == NR) {
			count = 0;
			++it;
		}
	};

	C2_UT_ASSERT(c2_vec_count(&t) == sum0);

	c2_vec_cursor_init(&c, &t);
	for (i = 0; i < sum0; ++i) {
		eov = c2_vec_cursor_move(&c, 1);
		C2_UT_ASSERT(eov == (i == sum0 - 1));
	}

	c2_vec_cursor_init(&c, &t);
	count = 0;
	it = 1;
	sum1 = 0;
	while (!c2_vec_cursor_move(&c, 0)) {
		if (count * it != 0) {
			step = c2_vec_cursor_step(&c);
			sum1 += step;
			C2_UT_ASSERT(step == count * it);
			eov = c2_vec_cursor_move(&c, step);
			C2_UT_ASSERT(eov == (sum1 == sum0));
		}
		if (++count == NR) {
			count = 0;
			++it;
		}
	}
	c2_vec_cursor_init(&c, &t);
	c2_vec_cursor_move(&c, sum0);
	C2_UT_ASSERT(c2_vec_cursor_move(&c, 0));

	C2_UT_ASSERT(c2_bufvec_alloc(&bv, NR, C2_SEG_SIZE) == 0);
	C2_UT_ASSERT(bv.ov_vec.v_nr == NR);
	for (i = 0; i < NR; ++i) {
		C2_UT_ASSERT(bv.ov_vec.v_count[i] == C2_SEG_SIZE);
		C2_UT_ASSERT(bv.ov_buf[i] != NULL);
	}
	c2_bufvec_free(&bv);
	C2_UT_ASSERT(bv.ov_vec.v_nr == 0);
	C2_UT_ASSERT(bv.ov_buf == NULL);
	c2_bufvec_free(&bv);    /* no-op */

	C2_UT_ASSERT(c2_bufvec_alloc_aligned(&bv, NR, C2_SEG_SIZE,
					      C2_SEG_SHIFT) == 0);
	C2_UT_ASSERT(bv.ov_vec.v_nr == NR);
	for (i = 0; i < NR; ++i) {
		C2_UT_ASSERT(bv.ov_vec.v_count[i] == C2_SEG_SIZE);
		C2_UT_ASSERT(bv.ov_buf[i] != NULL);
		C2_UT_ASSERT(c2_addr_is_aligned(bv.ov_buf[i], C2_SEG_SHIFT));
	}
	c2_bufvec_free_aligned(&bv, C2_SEG_SHIFT);
	C2_UT_ASSERT(bv.ov_vec.v_nr == 0);
	C2_UT_ASSERT(bv.ov_buf == NULL);
	c2_bufvec_free_aligned(&bv, C2_SEG_SHIFT);    /* no-op */

	test_bufvec_cursor();
}

static void test_bufvec_cursor(void)
{
	/* Create buffers with different shapes but same total size.
	   Also create identical buffers for exact shape testing,
	   and a couple of larger buffers whose bounds won't be reached.
	 */
	enum { NR_BUFS = 10 };
	static struct {
		uint32_t    num_segs;
		c2_bcount_t seg_size;
	} shapes[NR_BUFS] = {
		[0] = { 1, 48 },
		[1] = { 1, 48 },
		[2] = { 2, 24 },
		[3] = { 2, 24 },
		[4] = { 3, 16 },
		[5] = { 3, 16 },
		[6] = { 4, 12 },
		[7] = { 4, 12 },
		[8] = { 6,  8 },
		[9] = { 6,  8 },
	};
	static const char *msg = "abcdefghijklmnopqrstuvwxyz0123456789"
		"ABCDEFGHIJK";
	size_t msglen = strlen(msg)+1;
	struct c2_bufvec bufs[NR_BUFS];
	struct c2_bufvec *b;
	int i;

	C2_SET_ARR0(bufs);
	for (i = 0; i < NR_BUFS; ++i) {
		C2_UT_ASSERT(msglen == shapes[i].num_segs * shapes[i].seg_size);
		C2_UT_ASSERT(c2_bufvec_alloc(&bufs[i],
					     shapes[i].num_segs,
					     shapes[i].seg_size) == 0);
	}
	b = &bufs[0];
	C2_UT_ASSERT(b->ov_vec.v_nr == 1);
	memcpy(b->ov_buf[0], msg, msglen);
	C2_UT_ASSERT(memcmp(b->ov_buf[0], msg, msglen) == 0);
	for (i = 1; i < NR_BUFS; ++i) {
		struct c2_bufvec_cursor s_cur;
		struct c2_bufvec_cursor d_cur;
		int j;
		const char *p = msg;

		c2_bufvec_cursor_init(&s_cur, &bufs[i-1]);
		c2_bufvec_cursor_init(&d_cur, &bufs[i]);
		C2_UT_ASSERT(c2_bufvec_cursor_copy(&d_cur, &s_cur, msglen)
			     == msglen);

		/* verify cursor positions */
		C2_UT_ASSERT(c2_bufvec_cursor_move(&s_cur,0));
		C2_UT_ASSERT(c2_bufvec_cursor_move(&d_cur,0));

		/* verify data */
		for (j = 0; j < bufs[i].ov_vec.v_nr; ++j) {
			int k;
			char *q;
			for (k = 0; k < bufs[i].ov_vec.v_count[j]; ++k) {
				q = bufs[i].ov_buf[j] + k;
				C2_UT_ASSERT(*p++ == *q);
			}
		}
	}

	/* bounded copy - dest buffer smaller */
	{
		struct c2_bufvec buf;
		c2_bcount_t seg_size = shapes[NR_BUFS-1].seg_size - 1;
		uint32_t    num_segs = shapes[NR_BUFS-1].num_segs - 1;
		c2_bcount_t buflen = seg_size * num_segs;
		struct c2_bufvec_cursor s_cur;
		struct c2_bufvec_cursor d_cur;
		int j;
		const char *p = msg;
		int len;

		C2_UT_ASSERT(c2_bufvec_alloc(&buf, num_segs, seg_size) == 0);
		C2_UT_ASSERT(buflen < msglen);

		c2_bufvec_cursor_init(&s_cur, &bufs[NR_BUFS-1]);
		c2_bufvec_cursor_init(&d_cur, &buf);

		C2_UT_ASSERT(c2_bufvec_cursor_copy(&d_cur, &s_cur, msglen)
			     == buflen);

		/* verify cursor positions */
		C2_UT_ASSERT(!c2_bufvec_cursor_move(&s_cur,0));
		C2_UT_ASSERT(c2_bufvec_cursor_move(&d_cur,0));

		/* check partial copy correct */
		len = 0;
		for (j = 0; j < buf.ov_vec.v_nr; ++j) {
			int k;
			char *q;
			for (k = 0; k < buf.ov_vec.v_count[j]; ++k) {
				q = buf.ov_buf[j] + k;
				C2_UT_ASSERT(*p++ == *q);
				len++;
			}
		}
		C2_UT_ASSERT(len == buflen);
		c2_bufvec_free(&buf);
	}

	/* bounded copy - source buffer smaller */
	{
		struct c2_bufvec buf;
		c2_bcount_t seg_size = shapes[NR_BUFS-1].seg_size + 1;
		uint32_t    num_segs = shapes[NR_BUFS-1].num_segs + 1;
		c2_bcount_t buflen = seg_size * num_segs;
		struct c2_bufvec_cursor s_cur;
		struct c2_bufvec_cursor d_cur;
		int j;
		const char *p = msg;
		int len;

		C2_UT_ASSERT(c2_bufvec_alloc(&buf, num_segs, seg_size) == 0);
		C2_UT_ASSERT(buflen > msglen);

		c2_bufvec_cursor_init(&s_cur, &bufs[NR_BUFS-1]);
		c2_bufvec_cursor_init(&d_cur, &buf);

		C2_UT_ASSERT(c2_bufvec_cursor_copy(&d_cur, &s_cur, buflen)
			     == msglen);

		/* verify cursor positions */
		C2_UT_ASSERT(c2_bufvec_cursor_move(&s_cur,0));
		C2_UT_ASSERT(!c2_bufvec_cursor_move(&d_cur,0));

		/* check partial copy correct */
		len = 0;
		for (j = 0; j < buf.ov_vec.v_nr; ++j) {
			int k;
			char *q;
			for (k = 0; k < buf.ov_vec.v_count[j] && len < msglen;
			     k++) {
				q = buf.ov_buf[j] + k;
				C2_UT_ASSERT(*p++ == *q);
				len++;
			}
		}
		c2_bufvec_free(&buf);
	}

	/* free buffer pool */
	for (i = 0; i < ARRAY_SIZE(bufs); ++i)
		c2_bufvec_free(&bufs[i]);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
