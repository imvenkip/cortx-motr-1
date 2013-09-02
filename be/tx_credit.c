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
 * Original creation date: 17-Jun-2013
 */

#include "be/tx_credit.h"
#include "lib/assert.h"    /* M0_PRE */

/**
 * @addtogroup be
 *
 * @{
 */

struct m0_be_tx_credit m0_be_tx_credit_invalid =
	M0_BE_TX_CREDIT_INIT(M0_BCOUNT_MAX, M0_BCOUNT_MAX);

M0_INTERNAL void m0_be_tx_credit_add(struct m0_be_tx_credit *c0,
				     const struct m0_be_tx_credit *c1)
{
	c0->tc_reg_nr   += c1->tc_reg_nr;
	c0->tc_reg_size += c1->tc_reg_size;
}

M0_INTERNAL void m0_be_tx_credit_sub(struct m0_be_tx_credit *c0,
				     const struct m0_be_tx_credit *c1)
{
	M0_PRE(c0->tc_reg_nr   >= c1->tc_reg_nr);
	M0_PRE(c0->tc_reg_size >= c1->tc_reg_size);

	c0->tc_reg_nr   -= c1->tc_reg_nr;
	c0->tc_reg_size -= c1->tc_reg_size;
}

M0_INTERNAL void m0_be_tx_credit_mul(struct m0_be_tx_credit *c, m0_bcount_t k)
{
	c->tc_reg_nr   *= k;
	c->tc_reg_size *= k;
}

M0_INTERNAL void m0_be_tx_credit_mac(struct m0_be_tx_credit *c,
				     const struct m0_be_tx_credit *c1,
				     m0_bcount_t k)
{
	struct m0_be_tx_credit c1_k = *c1;

	m0_be_tx_credit_mul(&c1_k, k);
	m0_be_tx_credit_add(c, &c1_k);
}

M0_INTERNAL bool m0_be_tx_credit_le(const struct m0_be_tx_credit *c0,
				    const struct m0_be_tx_credit *c1)
{
	return c0->tc_reg_nr   <= c1->tc_reg_nr &&
	       c0->tc_reg_size <= c1->tc_reg_size;
}

M0_INTERNAL bool m0_be_tx_credit_eq(const struct m0_be_tx_credit *c0,
				    const struct m0_be_tx_credit *c1)
{
	return c0->tc_reg_nr   == c1->tc_reg_nr &&
	       c0->tc_reg_size == c1->tc_reg_size;
}

/** @} end of be group */

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
