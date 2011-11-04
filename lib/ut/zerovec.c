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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 * Original creation date: 09/27/2011
 */

#include "lib/ut.h"
#include "lib/vec.h"
#include "lib/memory.h"
#include "lib/arith.h"
#include "lib/cdefs.h"

#ifdef __KERNEL__
#include <linux/mm.h>
#endif

enum ZEROVEC_UT_VALUES {
	ZEROVEC_UT_SEG_SIZE = 1024,
	ZEROVEC_UT_SEGS_NR = 10,
};

static c2_bindex_t indices[ZEROVEC_UT_SEGS_NR];
static c2_bcount_t counts[ZEROVEC_UT_SEGS_NR];

static void zerovec_init(struct c2_0vec *zvec, const c2_bcount_t seg_size)
{
	int rc;

	rc = c2_0vec_init(zvec, ZEROVEC_UT_SEGS_NR, seg_size);
	C2_UT_ASSERT(rc == 0);
}

static void zerovec_init_bvec(void)
{
	int			rc;
	uint32_t		i;
	struct c2_0vec		zvec;
	struct c2_bufvec	bufvec;

	zerovec_init(&zvec, ZEROVEC_UT_SEG_SIZE);

	rc = c2_bufvec_alloc(&bufvec, ZEROVEC_UT_SEGS_NR, ZEROVEC_UT_SEG_SIZE);
	C2_UT_ASSERT(rc == 0);

	for (i = 0; i < ZEROVEC_UT_SEGS_NR; ++i)
		indices[i] = i;

	rc = c2_0vec_bvec_init(&zvec, &bufvec, indices);
	C2_UT_ASSERT(rc == 0);

	/* Checks if buffer array, index array and segment count array
	   are populated correctly. */
	for (i = 0; i < ZEROVEC_UT_SEGS_NR; ++i) {
		C2_UT_ASSERT(zvec.z_bvec.ov_buf[i] ==
			     bufvec.ov_buf[i]);
		C2_UT_ASSERT(zvec.z_bvec.ov_vec.v_count[i] ==
				ZEROVEC_UT_SEG_SIZE);
		C2_UT_ASSERT(zvec.z_indices[i] == indices[i]);
	}

	/* Tries to add bufvec beyond zerovec's limit. Should fail. */
	rc = c2_0vec_bvec_init(&zvec, &bufvec, indices);
	C2_UT_ASSERT(rc == -EMSGSIZE);

	c2_bufvec_free(&bufvec);
	c2_0vec_free(&zvec);
}

static void zerovec_init_bufs(void)
{
	int		  rc;
	char		**bufs;
	uint32_t	  i;
	uint64_t	  seed;
	struct c2_0vec	  zvec;

	seed = 0;
	zerovec_init(&zvec, ZEROVEC_UT_SEG_SIZE);

	C2_ALLOC_ARR(bufs, ZEROVEC_UT_SEGS_NR);
	C2_UT_ASSERT(bufs != NULL);

	for (i = 0; i < ZEROVEC_UT_SEGS_NR; ++i) {
		C2_ALLOC_ARR(bufs[i], ZEROVEC_UT_SEG_SIZE);
		C2_UT_ASSERT(bufs[i] != NULL);
		counts[i] = ZEROVEC_UT_SEG_SIZE;
		indices[i] = c2_rnd(ZEROVEC_UT_SEGS_NR, &seed);
	}

	rc = c2_0vec_bufs_init(&zvec, (void**)bufs, indices, counts,
			      ZEROVEC_UT_SEGS_NR);
	C2_UT_ASSERT(rc == 0);

	/* Checks if buffer array, index array and segment count array
	   are populated correctly. */
	for (i = 0; i < ZEROVEC_UT_SEGS_NR; ++i) {
		C2_UT_ASSERT(zvec.z_indices[i] == indices[i]);
		C2_UT_ASSERT(zvec.z_bvec.ov_vec.v_count[i] ==
			     counts[i]);
		C2_UT_ASSERT(zvec.z_bvec.ov_buf[i] == bufs[i]);
	}
	C2_UT_ASSERT(zvec.z_bvec.ov_vec.v_nr == ZEROVEC_UT_SEGS_NR);

	/* Tries to add buffers beyond zerovec's limit. Should fail. */
	rc = c2_0vec_bufs_init(&zvec, (void**)bufs, indices, counts,
			      ZEROVEC_UT_SEGS_NR);
	C2_UT_ASSERT(rc == -EMSGSIZE);

	for (i = 0; i < ZEROVEC_UT_SEGS_NR; ++i)
		c2_free(bufs[i]);
	c2_free(bufs);
	c2_0vec_free(&zvec);
}

static void zerovec_init_cbuf(void)
{
	int		i;
	int		rc;
	uint64_t	seed;
	struct c2_buf	bufs[ZEROVEC_UT_SEGS_NR];
	struct c2_0vec	zvec;

	seed = 0;
	zerovec_init(&zvec, ZEROVEC_UT_SEG_SIZE);

	for (i = 0; i < ZEROVEC_UT_SEGS_NR; ++i) {
		C2_ALLOC_ARR(bufs[i].b_addr, ZEROVEC_UT_SEG_SIZE);
		C2_UT_ASSERT(bufs[i].b_addr != NULL);
		bufs[i].b_nob = ZEROVEC_UT_SEG_SIZE;
		indices[i] = c2_rnd(ZEROVEC_UT_SEGS_NR, &seed);

		rc = c2_0vec_cbuf_add(&zvec, &bufs[i], &indices[i]);
		C2_UT_ASSERT(rc == 0);
	}

	for (i = 0; i < ZEROVEC_UT_SEGS_NR; ++i) {
		C2_UT_ASSERT(zvec.z_indices[i] == indices[i]);
		C2_UT_ASSERT(zvec.z_bvec.ov_buf[i] == bufs[i].b_addr);
		C2_UT_ASSERT(*zvec.z_bvec.ov_vec.v_count ==
			     bufs[i].b_nob);
	}

	/* Tries to add more buffers beyond zerovec's capacity. Should fail. */
	rc = c2_0vec_cbuf_add(&zvec, &bufs[0], &indices[0]);
	C2_UT_ASSERT(rc == -EMSGSIZE);

	C2_UT_ASSERT(zvec.z_bvec.ov_vec.v_nr == ZEROVEC_UT_SEGS_NR);

	for (i = 0; i < ZEROVEC_UT_SEGS_NR; ++i)
		c2_free(bufs[i].b_addr);
	c2_0vec_free(&zvec);
}

#ifdef __KERNEL__
static void zerovec_init_pages(void)
{
	int		rc;
	uint32_t	i;
	uint64_t	seed;
	struct page	pages[ZEROVEC_UT_SEGS_NR];
	struct c2_0vec	zvec;

	seed = 0;
	zerovec_init(&zvec, PAGE_SIZE);

	for (i = 0; i < ZEROVEC_UT_SEGS_NR; ++i) {
		indices[i] = c2_rnd(ZEROVEC_UT_SEGS_NR, &seed);
		rc = c2_0vec_page_add(&zvec, &pages[i], indices[i]);
		C2_UT_ASSERT(rc == 0);
	}

	for (i = 0; i < ZEROVEC_UT_SEGS_NR; ++i) {
		C2_UT_ASSERT(zvec.z_indices[i] == indices[i]);
		C2_UT_ASSERT(zvec.z_bvec.ov_buf[i] ==
			     page_address(&pages[i]));
		C2_UT_ASSERT(zvec.z_bvec.ov_vec.v_count[i] ==
			     PAGE_SIZE);
	}

	C2_UT_ASSERT(zvec.z_bvec.ov_vec.v_nr == ZEROVEC_UT_SEGS_NR);

	/* Tries to add more pages beyond zerovec's capacity. Should fail. */
	rc = c2_0vec_page_add(&zvec, &pages[0], indices[0]);
	C2_UT_ASSERT(rc == -EMSGSIZE);

	c2_0vec_free(&zvec);
}
#endif

void test_zerovec(void)
{
	/* Populate the zero vector using a c2_bufvec structure. */
	zerovec_init_bvec();

	/* Populate the zero vector using array of buffers, indices
	   and counts. */
	zerovec_init_bufs();

	/* Populate the zero vector using a c2_buf structure and
	   array of indices. */
	zerovec_init_cbuf();

#ifdef __KERNEL__
	/* Populate the zero vector using a page. */
	zerovec_init_pages();
#endif
}
