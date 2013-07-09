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
#include "be/tx_regmap.h"	/* m0_be_reg_area */
#include "be/tx_credit.h"	/* M0_BE_TX_CREDIT */
#include "ut/ut.h"		/* M0_UT_ASSERT */
#include "be/ut/helper.h"	/* m0_be_ut_seg_helper */
#include "lib/misc.h"		/* M0_BITS */

static struct m0_be_ut_h be_ut_seg_h;
static m0_bindex_t off = 256; /* slightly after the segment header */
static char buf[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
static struct m0_be_reg_d area[] = {
	{ .rd_reg = { .br_size =  8 }, .rd_buf = &buf[ 0] },
	{ .rd_reg = { .br_size =  8 }, .rd_buf = &buf[ 8] },
	{ .rd_reg = { .br_size = 16 }, .rd_buf = &buf[16] },
};

M0_INTERNAL void m0_be_ut_seg_init_fini(void)
{
	m0_be_ut_seg_storage_init();
	m0_be_ut_seg_initialize(&be_ut_seg_h, true);
	m0_be_ut_seg_finalize(&be_ut_seg_h, true);
	m0_be_ut_seg_storage_fini();
}

static void seg_write(struct m0_be_seg *seg)
{
	struct m0_be_reg_area ra;
	struct m0_be_op	      op;
	int		      i;
	int		      rc;

	m0_be_op_init(&op);

	/* copy buf to the segment */
	memcpy((char *) seg->bs_addr + off, buf, ARRAY_SIZE(buf));

	for (i = 0; i < ARRAY_SIZE(area); ++i) {
		area[i].rd_buf	       = NULL;
		area[i].rd_reg.br_addr = (char *) seg->bs_addr + off + i*8;
		area[i].rd_reg.br_seg  = seg;
	}

	rc = m0_be_reg_area_init(&ra, &M0_BE_TX_CREDIT(ARRAY_SIZE(area),
						       ARRAY_SIZE(buf)));
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < ARRAY_SIZE(area); ++i)
		m0_be_reg_area_capture(&ra, &area[i]);
	m0_be_seg_write_simple(seg, &op, &ra);
	m0_be_op_wait(&op);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);

	m0_be_reg_area_fini(&ra);
	m0_be_op_fini(&op);

	for (i = 0; i < ARRAY_SIZE(area); ++i)
		area[i].rd_buf = &buf[i*8];
}

M0_INTERNAL void m0_be_ut_seg_create_destroy(void)
{
	m0_be_ut_seg_create(&be_ut_seg_h);
	m0_be_ut_seg_destroy(&be_ut_seg_h);
}

M0_INTERNAL void m0_be_ut_seg_open_close(void)
{
	m0_be_ut_seg_create_open(&be_ut_seg_h);
	m0_be_ut_seg_close_destroy(&be_ut_seg_h);
}

M0_INTERNAL void m0_be_ut_seg_write(void)
{
	int rc;
	int i;

	m0_be_ut_seg_create_open(&be_ut_seg_h);
	seg_write(&be_ut_seg_h.buh_seg);
	m0_be_seg_close(&be_ut_seg_h.buh_seg);
	rc = m0_be_seg_destroy(&be_ut_seg_h.buh_seg);
	M0_UT_ASSERT(rc == 0);
	m0_be_ut_seg_finalize(&be_ut_seg_h, false);


	m0_be_ut_seg_initialize(&be_ut_seg_h, true);
	rc = m0_be_seg_open(&be_ut_seg_h.buh_seg);
	M0_ASSERT(rc == 0);

	for (i = 0; i < ARRAY_SIZE(area); ++i)
		M0_UT_ASSERT(memcmp(area[i].rd_buf,
				    area[i].rd_reg.br_addr,
				    area[i].rd_reg.br_size) == 0);

	m0_be_ut_seg_close_destroy(&be_ut_seg_h);
}
