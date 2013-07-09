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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 2-Jul-2013
 */

#include "be/tx_regmap.h"
#include "be/ut/helper.h"
#include "be/tx.h"
#include "ut/ut.h"
#include "be/be.h"

void m0_be_ut_log(void)
{
#if 0
	struct m0_be_ut_h      hseg;
	struct m0_be_log       log;
	struct m0_be_tx_group  gr;
	struct m0_be_tx        tx[3];
	struct m0_be_reg_d     rd[1+2+3];
	struct m0_be_op        op;
	int		       i;
	int		       j;
	int                    rc;

	m0_be_ut_seg_create_open(&hseg);

	for (i = 0; i < ARRAY_SIZE(tx); ++i) {
		rc = m0_be_reg_area_init(&tx[i].t_reg_area,
					 &M0_BE_TX_CREDIT(i+1, 10*(i+1)));
		M0_UT_ASSERT(rc == 0);
		grp_tlink_init(&tx[i]);

		for (j = 0; j < i+1; ++j) {
			M0_UT_ASSERT(j < ARRAY_SIZE(rd));
			rd[j] = (struct m0_be_reg_d) {
				.rd_tx	= &tx[i],
				.rd_buf = NULL,
				.rd_reg = M0_BE_REG(&hseg.buh_seg, i+2,
						    &hseg.buh_seg.bs_addr +
						    i*100 + j*10)
			};
			m0_be_reg_area_capture(&tx[i].t_reg_area, &rd[j]);
		}
	}

	m0_be_log_init(&log);
	rc = m0_be_log_create(&log, 1 << 20);
	M0_UT_ASSERT(rc == 0);

	rc = tx_group_init(&gr, &log);
	M0_UT_ASSERT(rc == 0);

	for (i = 0; i < ARRAY_SIZE(tx); ++i) {
		grp_tlist_add(&gr.tg_txs, &tx[i]);
		m0_be_log_reserve_tx(&log, &M0_BE_TX_CREDIT(i+1, 10*(i+1)));
	}

	m0_be_op_init(&op);
	m0_be_log_submit(&log, &op, &gr);
	m0_be_op_wait(&op);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);

	m0_be_log_destroy(&log);
	m0_be_log_fini(&log);

	for (i = 0; i < ARRAY_SIZE(tx); ++i) {
		grp_tlist_del(&tx[i]);
		grp_tlink_fini(&tx[i]);
		m0_be_reg_area_fini(&tx[i].t_reg_area);
	}

	tx_group_fini(&gr);

	m0_be_ut_seg_close_destroy(&hseg);
#endif
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
