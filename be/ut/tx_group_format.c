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

#include "be/tx_group_format.h"

#include "ut/ut.h"
#include "ut/stob.h"		/* m0_ut_stob_linux_get */
#include "be/ut/helper.h"

#include "be/tx_group.h"	/* grp_tlist_init */

enum {
	BE_UT_TX_GROUP_ONDISK_SEG_SIZE = 0x10000,
	BE_UT_TX_GROUP_ONDISK_ITER     = 0x100,
	BE_UT_TX_GROUP_ONDISK_LOG_SIZE = 1 << 11,
	BE_UT_TX_GROUP_ONDISK_RB_SIZE  = 0x1000
};

/* XXX rename */
struct m0_be_tx        but_group_format_tx[3];
struct m0_be_log       but_group_format_log;
struct m0_be_tx_group  but_group_format_gr;

struct m0_be_tx_credit but_group_format_logged[BE_UT_TX_GROUP_ONDISK_RB_SIZE];
m0_bindex_t	       but_group_format_begin;
m0_bindex_t	       but_group_format_end;

static void be_ut_group_format_rb_init(void)
{
	but_group_format_begin = 0;
	but_group_format_end = 0;
}

static void be_ut_group_format_rb_fini(void)
{
	M0_UT_ASSERT(but_group_format_begin == but_group_format_end);
}

static bool be_ut_group_format_rb__invariant(void)
{
	return but_group_format_end <= but_group_format_begin &&
		(but_group_format_begin - but_group_format_end) <=
		ARRAY_SIZE(but_group_format_logged);
}

static void be_ut_group_format_push(struct m0_be_tx_credit *credit)
{
	M0_PRE(be_ut_group_format_rb__invariant());
	but_group_format_logged[but_group_format_begin++ %
		ARRAY_SIZE(but_group_format_logged)] = *credit;
	M0_POST(be_ut_group_format_rb__invariant());
}

static void be_ut_group_format_pop(struct m0_be_tx_credit *credit)
{
	M0_PRE(be_ut_group_format_rb__invariant());
	*credit = but_group_format_logged[but_group_format_end++ %
		ARRAY_SIZE(but_group_format_logged)];
	M0_POST(be_ut_group_format_rb__invariant());
}

static void be_ut_group_format_log(void)
{
	M0_BE_OP_SYNC(op, m0_be_log_submit(&but_group_format_log, &op,
					   &but_group_format_gr));
	M0_BE_OP_SYNC(op, m0_be_log_commit(&but_group_format_log, &op,
					   &but_group_format_gr));
}

static void be_ut_group_format_log_discard(void)
{
	struct m0_be_tx_credit reserved;

	be_ut_group_format_pop(&reserved);
	m0_be_log_discard(&but_group_format_log, &reserved);
}

static int be_ut_group_format_reserve(void)
{
	int i;
	int rc;

	for (i = 0; i < ARRAY_SIZE(but_group_format_tx); ++i) {
		rc = m0_be_log_reserve_tx(&but_group_format_log,
					  &M0_BE_TX_CREDIT(i + 1,
							   10 * (i + 1)), 0);
		if (rc != 0)
			return i;
		grp_tlist_add(&but_group_format_gr.tg_txs,
			      &but_group_format_tx[i]);
	}
	return i;
}

void m0_be_ut_group_format(void)
{
	struct m0_be_tx_credit gr_credit = {};
	struct m0_be_tx_credit tx_credit;
	struct m0_be_tx_credit reserved;
	struct m0_be_reg_d     rd[1+2+3];
	struct m0_be_ut_seg    ut_seg;
	struct m0_be_seg      *seg;
	struct m0_stob	      *stob;
	int                    i;
	int                    j;
	int                    rc;
	int                    groups_logged;
	int                    tx_reserved;

	be_ut_group_format_rb_init();
	m0_be_ut_seg_init(&ut_seg, NULL, BE_UT_TX_GROUP_ONDISK_SEG_SIZE);
	seg = ut_seg.bus_seg;

	for (i = 0; i < ARRAY_SIZE(but_group_format_tx); ++i) {
		tx_credit = M0_BE_TX_CREDIT(i + 1, 10 * (i + 1));
		rc = m0_be_reg_area_init(&but_group_format_tx[i].t_reg_area,
					 &tx_credit, true);
		m0_be_tx_credit_add(&gr_credit, &tx_credit);
		M0_UT_ASSERT(rc == 0);
		grp_tlink_init(&but_group_format_tx[i]);

		for (j = 0; j < i+1; ++j) {
			M0_UT_ASSERT(j < ARRAY_SIZE(rd));
			rd[j] = (struct m0_be_reg_d) {
				.rd_reg = M0_BE_REG(seg, i+2,
						    seg->bs_addr + i*100 + j*10)
			};
			m0_be_reg_area_capture(&but_group_format_tx[i].
						t_reg_area, &rd[j]);
		}
	}

	stob = m0_ut_stob_linux_get();
	m0_be_log_init(&but_group_format_log, stob, /* XXX */ NULL);
	rc = m0_be_log_create(&but_group_format_log,
			      BE_UT_TX_GROUP_ONDISK_LOG_SIZE);
	M0_UT_ASSERT(rc == 0);

	rc = m0_be_group_format_init(&but_group_format_gr.tg_od,
				     m0_be_log_stob(&but_group_format_log),
				     ARRAY_SIZE(but_group_format_tx),
				     &gr_credit, 1);
	M0_UT_ASSERT(rc == 0);
	grp_tlist_init(&but_group_format_gr.tg_txs);

	groups_logged = 0;
	for (i = 0; i < BE_UT_TX_GROUP_ONDISK_ITER; ++i) {
		tx_reserved = be_ut_group_format_reserve();
		if (tx_reserved != 0) {
			m0_be_group_format_io_reserved(
				&but_group_format_gr.tg_od,
				&but_group_format_gr, &reserved);
			be_ut_group_format_push(&reserved);
			be_ut_group_format_log();
			++groups_logged;
		} else {
			be_ut_group_format_log_discard();
			--groups_logged;
		}
		for (j = 0; j < tx_reserved; ++j)
			grp_tlist_del(&but_group_format_tx[j]);
		m0_be_group_format_reset(&but_group_format_gr.tg_od);
	}
	for (i = 0; i < groups_logged; ++i)
		be_ut_group_format_log_discard();

	for (i = 0; i < ARRAY_SIZE(but_group_format_tx); ++i) {
		grp_tlink_fini(&but_group_format_tx[i]);
		m0_be_reg_area_fini(&but_group_format_tx[i].t_reg_area);
	}
	grp_tlist_fini(&but_group_format_gr.tg_txs);

	m0_be_group_format_fini(&but_group_format_gr.tg_od);

	m0_be_log_destroy(&but_group_format_log);
	m0_be_log_fini(&but_group_format_log);
	m0_ut_stob_put(stob, true);

	m0_be_ut_seg_fini(&ut_seg);
	be_ut_group_format_rb_fini();
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
