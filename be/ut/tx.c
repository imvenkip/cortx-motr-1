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
 * Original creation date: 12-Jun-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/tx_fom.h"
#include "lib/types.h"     /* m0_uint128_eq */
#include "lib/misc.h"      /* M0_BITS, M0_IN */
#include "be/ut/helper.h"
#include "ut/ut.h"

void m0_be_ut_tx_simple(void)
{
	struct m0_be_tx_credit cred;
	struct m0_be_ut_h      h;
	struct m0_uint128     *p;
	struct m0_uint128      p_save;
	struct m0_be_op        op;
	struct m0_be_tx        tx = {};
	int                    rc;

	M0_ENTRY();
	/*
	 * Init BE, BE IO, credits
	 */
	m0_be_ut_h_init(&h);

	m0_be_op_init(&op);
	m0_be_tx_credit_init(&cred);

	/*
	 * Init transaction and its credits
	 */
	m0_be_ut_h_tx_init(&tx, &h);
	m0_be_allocator_credit(h.buh_a, M0_BAO_ALLOC, sizeof *p, 0, &cred);

	m0_sm_group_lock(&ut__txs_sm_group);

	m0_be_tx_prep(&tx, &cred);

	/* Open transaction, allocate, dirty and capture region. */
	m0_be_tx_open(&tx);
	rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_ACTIVE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_LOG(M0_DEBUG, "Transaction has reached M0_BTS_ACTIVE");

	p = m0_be_alloc(h.buh_a, &tx, &op, sizeof *p, 0);
	M0_UT_ASSERT(p != NULL);
	M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
						 M0_BOS_FAILURE)));
	p->u_hi = 0xdeadd00d8badf00d;
	p->u_lo = 0x5ca1ab1e7e1eca57;
	p_save = *p;
	M0_BE_TX_CAPTURE_PTR(&h.buh_seg, &tx, p);
	p->u_hi = 0;
	p->u_lo = 0;
	M0_UT_ASSERT(!m0_uint128_eq(p, &p_save));

	m0_be_tx_close(&tx); /* Make things persistent. */

	rc = m0_be_tx_timedwait(&tx, M0_BITS(M0_BTS_PLACED), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_LOG(M0_DEBUG, "Transaction has reached M0_BTS_PLACED");

	/* XXX TODO: m0_be_tx_stable(tx) */

	m0_sm_group_unlock(&ut__txs_sm_group);

	/*
	 * Reload segment and check data
	 */
	m0_be_ut_h_seg_reload(&h);
	M0_UT_ASSERT(m0_uint128_eq(p, &p_save));
	M0_LOG(M0_DEBUG, "Segment data is reloaded properly");

	/** XXX TODO m0_be_free() */
	m0_be_ut_h_fini(&h);

	M0_LEAVE();
}

#undef M0_TRACE_SUBSYSTEM
