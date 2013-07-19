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

#pragma once
#ifndef __MERO_BE_TX_CREDIT_H__
#define __MERO_BE_TX_CREDIT_H__

#include "lib/types.h"	/* m0_bcount_t */

/**
 * @defgroup be
 *
 * @{
 */

/**
 * Credit represents resources that a transaction could consume:
 *
 *     - for each region captured by an active transaction, contents of captured
 *       region must be stored somewhere (to be written to the log later). That
 *       requires memory, which must be pre-allocated before transaction
 *       captures anything to avoid dead-locks;
 *
 *     - similarly, for each captured region, a fixed size region descriptor
 *       (m0_be_reg_d) should be stored. The memory for the descriptor must be
 *       pre-allocated;
 *
 *     - finally, before transaction captures anything, transaction engine must
 *       assure that there is enough free space in the log to write
 *       transaction's updates. The space required is proportional to total
 *       number of regions captured by the transaction and to total size of
 *       these regions.
 *
 * Hence, the user should inform the engine about amount and size of regions
 * that the transaction would modify. This is achieved by calling
 * m0_be_tx_prep() (possibly multiple times), while the transaction is in
 * PREPARE state. The calls to m0_be_tx_prep() must be conservative: it is fine
 * to prepare for more updates than the transaction will actually make (the
 * latter quantity is usually impossible to know beforehand anyway), but the
 * transaction must never capture more than it prepared.
 */
struct m0_be_tx_credit {
	/**
	 * The number of regions needed for operation representation in the
	 * transaction.
	 */
	m0_bcount_t tc_reg_nr;
	/** Total size of memory needed for the same. */
	m0_bcount_t tc_reg_size;
};

/**
 * @todo XXX it should have name m0_be_tx_credit_reset() because
 * there is no fini() function for m0_be_tx_credit
 */
M0_INTERNAL void m0_be_tx_credit_init(struct m0_be_tx_credit *credit);

/** c0 += c1 */
M0_INTERNAL void m0_be_tx_credit_add(struct m0_be_tx_credit *c0,
				     const struct m0_be_tx_credit *c1);

/** c0 -= c1 */
M0_INTERNAL void m0_be_tx_credit_sub(struct m0_be_tx_credit *c0,
				     const struct m0_be_tx_credit *c1);

/** c *= k */
M0_INTERNAL void m0_be_tx_credit_mul(struct m0_be_tx_credit *c, m0_bcount_t k);

/**
 * c += c1 * k
 * Multiply-accumulate operation.
 */
M0_INTERNAL void m0_be_tx_credit_mac(struct m0_be_tx_credit *c,
				     const struct m0_be_tx_credit *c1,
				     m0_bcount_t k);

M0_INTERNAL bool m0_be_tx_credit_le(const struct m0_be_tx_credit *c0,
				    const struct m0_be_tx_credit *c1);

#define M0_BE_TX_CREDIT_ZERO			\
	((struct m0_be_tx_credit) {		\
		.tc_reg_nr = 0,			\
		.tc_reg_size = 0		\
	})

#define M0_BE_TX_CREDIT_TYPE(type)		\
	((struct m0_be_tx_credit) {		\
		.tc_reg_nr = 1,			\
		.tc_reg_size = sizeof (type)	\
	})

#define M0_BE_TX_CREDIT(reg_nr, reg_size)	\
	((struct m0_be_tx_credit) {		\
		.tc_reg_nr = (reg_nr),		\
		.tc_reg_size = (reg_size)	\
	})

/** @} end of be group */
#endif /* __MERO_BE_TX_CREDIT_H__ */

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
