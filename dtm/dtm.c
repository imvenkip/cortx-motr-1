/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 04/01/2010
 */

/**
 * @addtogroup dtm Distributed transaction manager
 * @{
 */

#include "lib/misc.h"              /* C2_SET0 */
#include "dtm/dtm.h"

void c2_dtx_init(struct c2_dtx *tx)
{
	C2_SET0(tx);
	tx->tx_state = C2_DTX_INIT;
}

int c2_dtx_open(struct c2_dtx *tx, struct c2_dbenv *env)
{
	int result;

	C2_PRE(tx->tx_state == C2_DTX_INIT);

	result = c2_db_tx_init(&tx->tx_dbtx, env, 0);
	if (result == 0)
		tx->tx_state = C2_DTX_OPEN;
	return result;
}

void c2_dtx_done(struct c2_dtx *tx)
{
	C2_PRE(tx->tx_state == C2_DTX_INIT || tx->tx_state == C2_DTX_OPEN);

	if (tx->tx_state == C2_DTX_OPEN)
		c2_db_tx_commit(&tx->tx_dbtx);
	tx->tx_state = C2_DTX_DONE;
}

/** @} end of dtm group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
