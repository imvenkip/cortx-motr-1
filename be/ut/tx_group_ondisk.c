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
 * Original author: Maxim Medved <Max_Medved@xyratex.com>
 * Original creation date: 4-Jul-2013
 */

#include "be/tx_group_ondisk.c"

#include "ut/ut.h"
#include "be/ut/helper.h"

enum {
	BE_UT_TX_GROUP_ONDISK_SEG_SIZE = 0x10000,
	BE_UT_TX_GROUP_ONDISK_ITER     = 0x100,
	BE_UT_TX_GROUP_ONDISK_LOG_SIZE = 1 << 11,
	BE_UT_TX_GROUP_ONDISK_RB_SIZE  = 0x1000,
};

/* XXX rename */
struct m0_be_tx        but_group_ondisk_tx[3];
struct m0_be_log       but_group_ondisk_log;
struct m0_be_tx_group  but_group_ondisk_gr;

struct m0_be_tx_credit but_group_ondisk_logged[BE_UT_TX_GROUP_ONDISK_RB_SIZE];
m0_bindex_t	       but_group_ondisk_begin;
m0_bindex_t	       but_group_ondisk_end;

static void be_ut_group_ondisk_rb_init(void)
{
	but_group_ondisk_begin = 0;
	but_group_ondisk_end = 0;
}

static void be_ut_group_ondisk_rb_fini(void)
{
	M0_UT_ASSERT(but_group_ondisk_begin == but_group_ondisk_end);
}

static bool be_ut_group_ondisk_rb__invariant(void)
{
	return but_group_ondisk_end <= but_group_ondisk_begin &&
		(but_group_ondisk_begin - but_group_ondisk_end) <=
		ARRAY_SIZE(but_group_ondisk_logged);
}

static void be_ut_group_ondisk_push(struct m0_be_tx_credit *credit)
{
	M0_PRE(be_ut_group_ondisk_rb__invariant());
	but_group_ondisk_logged[but_group_ondisk_begin++ %
		ARRAY_SIZE(but_group_ondisk_logged)] = *credit;
	M0_POST(be_ut_group_ondisk_rb__invariant());
}

static void be_ut_group_ondisk_pop(struct m0_be_tx_credit *credit)
{
	M0_PRE(be_ut_group_ondisk_rb__invariant());
	*credit = but_group_ondisk_logged[but_group_ondisk_end++ %
		ARRAY_SIZE(but_group_ondisk_logged)];
	M0_POST(be_ut_group_ondisk_rb__invariant());
}

static void be_ut_group_ondisk_log(void)
{
	struct m0_be_op op;
	int             rc;

	m0_be_op_init(&op);
	m0_be_log_submit(&but_group_ondisk_log, &op, &but_group_ondisk_gr);
	rc = m0_be_op_wait(&op);
	M0_UT_ASSERT(rc == 0);
	m0_be_op_fini(&op);

	m0_be_op_init(&op);
	m0_be_log_commit(&but_group_ondisk_log, &op, &but_group_ondisk_gr);
	rc = m0_be_op_wait(&op);
	M0_UT_ASSERT(rc == 0);
	m0_be_op_fini(&op);
}

static void be_ut_group_ondisk_log_discard(void)
{
	struct m0_be_tx_credit reserved;

	be_ut_group_ondisk_pop(&reserved);
	m0_be_log_discard(&but_group_ondisk_log, &reserved);
}

static int be_ut_group_ondisk_reserve(void)
{
	int i;
	int rc;

	for (i = 0; i < ARRAY_SIZE(but_group_ondisk_tx); ++i) {
		rc = m0_be_log_reserve_tx(&but_group_ondisk_log,
					  &M0_BE_TX_CREDIT_OBJ(i + 1,
							       10 * (i + 1)));
		if (rc != 0)
			return i;
		grp_tlist_add(&but_group_ondisk_gr.tg_txs,
			      &but_group_ondisk_tx[i]);
	}
	return i;
}

void m0_be_ut_group_ondisk(void)
{
	M0_BE_TX_CREDIT(gr_credit);
	struct m0_be_tx_credit tx_credit;
	struct m0_be_tx_credit reserved;
	struct m0_be_reg_d     rd[1+2+3];
	struct m0_be_ut_seg    ut_seg;
	struct m0_be_seg      *seg = &ut_seg.bus_seg;
	int                    i;
	int                    j;
	int                    rc;
	int                    groups_logged;
	int                    tx_reserved;

	be_ut_group_ondisk_rb_init();
	m0_be_ut_seg_init(&ut_seg, BE_UT_TX_GROUP_ONDISK_SEG_SIZE);

	for (i = 0; i < ARRAY_SIZE(but_group_ondisk_tx); ++i) {
		tx_credit = M0_BE_TX_CREDIT_OBJ(i + 1, 10 * (i + 1));
		rc = m0_be_reg_area_init(&but_group_ondisk_tx[i].t_reg_area,
					 &tx_credit, true);
		m0_be_tx_credit_add(&gr_credit, &tx_credit);
		M0_UT_ASSERT(rc == 0);
		grp_tlink_init(&but_group_ondisk_tx[i]);

		for (j = 0; j < i+1; ++j) {
			M0_UT_ASSERT(j < ARRAY_SIZE(rd));
			rd[j] = (struct m0_be_reg_d) {
				.rd_tx	= &but_group_ondisk_tx[i],
				.rd_reg = M0_BE_REG(seg, i+2, (char *)
						    seg->bs_addr + i*100 + j*10)
			};
			m0_be_reg_area_capture(&but_group_ondisk_tx[i].
						t_reg_area, &rd[j]);
		}
	}

	m0_be_log_init(&but_group_ondisk_log, /* XXX */ NULL);
	rc = m0_be_log_create(&but_group_ondisk_log,
			      BE_UT_TX_GROUP_ONDISK_LOG_SIZE);
	M0_UT_ASSERT(rc == 0);

	rc = m0_be_group_ondisk_init(&but_group_ondisk_gr.tg_od,
				     m0_be_log_stob(&but_group_ondisk_log),
				     ARRAY_SIZE(but_group_ondisk_tx),
				     &gr_credit);
	M0_UT_ASSERT(rc == 0);
	grp_tlist_init(&but_group_ondisk_gr.tg_txs);

	groups_logged = 0;
	for (i = 0; i < BE_UT_TX_GROUP_ONDISK_ITER; ++i) {
		tx_reserved = be_ut_group_ondisk_reserve();
		if (tx_reserved != 0) {
			m0_be_group_ondisk_io_reserved(
				&but_group_ondisk_gr.tg_od,
				&but_group_ondisk_gr, &reserved);
			be_ut_group_ondisk_push(&reserved);
			be_ut_group_ondisk_log();
			++groups_logged;
		} else {
			be_ut_group_ondisk_log_discard();
			--groups_logged;
		}
		for (j = 0; j < tx_reserved; ++j)
			grp_tlist_del(&but_group_ondisk_tx[j]);
		m0_be_group_ondisk_reset(&but_group_ondisk_gr.tg_od);
	}
	for (i = 0; i < groups_logged; ++i)
		be_ut_group_ondisk_log_discard();

	m0_be_log_destroy(&but_group_ondisk_log);
	m0_be_log_fini(&but_group_ondisk_log);

	for (i = 0; i < ARRAY_SIZE(but_group_ondisk_tx); ++i) {
		grp_tlink_fini(&but_group_ondisk_tx[i]);
		m0_be_reg_area_fini(&but_group_ondisk_tx[i].t_reg_area);
	}

	grp_tlist_fini(&but_group_ondisk_gr.tg_txs);

	m0_be_ut_seg_fini(&ut_seg);
	be_ut_group_ondisk_rb_fini();
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
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
