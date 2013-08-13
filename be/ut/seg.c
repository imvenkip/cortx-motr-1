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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 29-May-2013
 */

#include "be/seg.h"		/* m0_be_seg */

#include "ut/ut.h"		/* M0_UT_ASSERT */
#include "be/ut/helper.h"	/* m0_be_ut_seg_helper */

enum {
	BE_UT_SEG_SIZE	  = 0x20000,
	BE_UT_SEG_IO_ITER = 0x400,
	BE_UT_SEG_IO_OFFS = 0x10000,
	BE_UT_SEG_IO_SIZE = 0x10000,
};
M0_BASSERT(BE_UT_SEG_IO_OFFS + BE_UT_SEG_IO_SIZE <= BE_UT_SEG_SIZE);

M0_INTERNAL void m0_be_ut_seg_open_close(void)
{
	struct m0_be_ut_seg ut_seg;

	m0_be_ut_seg_init(&ut_seg, BE_UT_SEG_SIZE);
	m0_be_ut_seg_fini(&ut_seg);
}

static void be_ut_seg_rand_reg(struct m0_be_reg *reg,
			       void *seg_addr,
			       m0_bindex_t *offset,
			       m0_bcount_t *size,
			       unsigned *seed)
{
	*size	= rand_r(seed) % (BE_UT_SEG_IO_SIZE / 2) + 1;
	*offset = rand_r(seed) % (BE_UT_SEG_IO_SIZE / 2 - 1);
	reg->br_addr = seg_addr + BE_UT_SEG_IO_OFFS + *offset;
	reg->br_size = *size;
}

M0_INTERNAL void m0_be_ut_seg_io(void)
{
	struct m0_be_ut_seg ut_seg;
	struct m0_be_seg   *seg;
	struct m0_be_reg    reg;
	struct m0_be_reg    reg_check;
	m0_bindex_t	    offset;
	m0_bcount_t	    size;
	static char	    pre[BE_UT_SEG_IO_SIZE];
	static char	    post[BE_UT_SEG_IO_SIZE];
	static char	    rand[BE_UT_SEG_IO_SIZE];
	unsigned	    seed;
	int		    rc;
	int		    i;
	int		    j;
	int		    cmp;

	seed = 0;
	m0_be_ut_seg_init(&ut_seg, BE_UT_SEG_SIZE);
	seg = &ut_seg.bus_seg;
	reg_check = M0_BE_REG(seg, BE_UT_SEG_IO_SIZE,
			      seg->bs_addr + BE_UT_SEG_IO_OFFS);
	for (i = 0; i < BE_UT_SEG_IO_ITER; ++i) {
		be_ut_seg_rand_reg(&reg, seg->bs_addr, &offset, &size, &seed);
		reg.br_seg = seg;
		for (j = 0; j < reg.br_size; ++j)
			rand[j] = rand_r(&seed) & 0xFF;

		/* read segment before write operation */
		rc = m0_be_seg__read(&reg_check, pre);
		M0_UT_ASSERT(rc == 0);
		/* write */
		rc = m0_be_seg__write(&reg, rand);
		M0_UT_ASSERT(rc == 0);
		/* and read to check if it was written */
		rc = m0_be_seg__read(&reg_check, post);
		/* reload segment to test I/O operations in open()/close() */
		m0_be_seg_close(seg);
		rc = m0_be_seg_open(seg);
		M0_UT_ASSERT(rc == 0);

		for (j = 0; j < size; ++j)
			pre[j + offset] = rand[j];

		M0_CASSERT(ARRAY_SIZE(pre) == ARRAY_SIZE(post));
		/*
		 * check if data was written to stob
		 * just after write operation
		 */
		cmp = memcmp(pre, post, ARRAY_SIZE(pre));
		M0_UT_ASSERT(cmp == 0);
		/* compare segment contents before and after reload */
		cmp = memcmp(post, reg_check.br_addr, reg_check.br_size);
		M0_UT_ASSERT(cmp == 0);
	}
	m0_be_ut_seg_fini(&ut_seg);
}
